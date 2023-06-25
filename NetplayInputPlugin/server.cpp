#include "stdafx.h"

#include "server.h"
#include "room.h"
#include "user.h"
#include "version.h"

using namespace std;
using namespace asio;

server::server(io_service& service, bool multiroom) :
     service(&service), multiroom(multiroom), acceptor(service), udp_socket(service), timer(service) {
#ifdef _WIN32
    QOS_VERSION version;
    version.MajorVersion = 1;
    version.MinorVersion = 0;
    QOSCreateHandle(&version, &qos_handle);
#endif
}

uint16_t server::open(uint16_t port) {
    error_code error;

    auto ipv_tcp = ip::tcp::v6();
    auto ipv_udp = ip::udp::v6();
    acceptor.open(ipv_tcp, error);
    if (error) { // IPv6 not available
        ipv_tcp = ip::tcp::v4();
        ipv_udp = ip::udp::v4();
        acceptor.open(ipv_tcp, error);
        if (error) throw error;
    }

    acceptor.bind(ip::tcp::endpoint(ipv_tcp, port));
    acceptor.listen();

    udp_socket.open(ipv_udp);
    udp_socket.bind(ip::udp::endpoint(ipv_udp, acceptor.local_endpoint().port()));

    accept();
    read();

    on_tick();

    log("Listening on port " + to_string(acceptor.local_endpoint().port()) + "...");

    return acceptor.local_endpoint().port();
}

void server::close() {
    if (acceptor.is_open()) {
        error_code error;
        acceptor.close(error);
    }

    if (udp_socket.is_open()) {
        udp_socket.close();
    }

    timer.cancel();

    auto r = rooms;
    rooms.clear();
    for (auto& e : r) {
        e.second->close();
    }
}

void server::accept() {
    auto u = make_shared<user>(this);
    acceptor.async_accept(*(u->tcp_socket), [=](error_code error) {
        if (error) return log(cerr, error.message());

        auto ep = u->tcp_socket->remote_endpoint(error);
        if (error) return accept();

        u->address = endpoint_to_string(ep, true);

        u->tcp_socket->set_option(ip::tcp::no_delay(true), error);
        if (error) return accept();

#ifdef _WIN32
        if (qos_handle != NULL) {
            QOS_FLOWID flowId = 0;
            QOSAddSocketToFlow(qos_handle, u->tcp_socket->native_handle(), u->tcp_socket->remote_endpoint().data(), QOSTrafficTypeAudioVideo, QOS_NON_ADAPTIVE_FLOW, &flowId);
        }
#else
        if (u->tcp_socket->local_endpoint().address().is_v6()) {
            u->tcp_socket->set_option(asio::detail::socket_option::integer<IPPROTO_IPV6, IPV6_TCLASS>(40 << 2), error);
        } else {
            u->tcp_socket->set_option(asio::detail::socket_option::integer<IPPROTO_IP, IP_TOS>(40 << 2), error);
        }
        if (error) return accept();
#endif

        users[u.get()] = u;
        u->send_protocol_version();
        u->receive_tcp_packet();

        accept();
    });
}

void server::read() {
    udp_socket.async_wait(ip::udp::socket::wait_read, [=](const error_code& error) {
        if (error) return;
        while (udp_socket.available()) {
            packet p(udp_socket.available());
            ip::udp::endpoint udp_remote_endpoint;
            p.resize(udp_socket.receive_from(buffer(p), udp_remote_endpoint));
            if (p.empty()) continue;
            switch (p.read<query_type>()) {
                case SERVER_PING: {
                    packet pong;
                    pong << SERVER_PONG << PROTOCOL_VERSION;
                    while (p.available()) {
                        pong << p.read<uint8_t>();
                    }
                    error_code error;
                    udp_socket.send_to(buffer(pong), udp_remote_endpoint, 0, error);
                    if (error) return;
                    break;
                }

                case EXTERNAL_ADDRESS: {
                    packet p;
                    p << EXTERNAL_ADDRESS << udp_remote_endpoint.port();
                    auto addr = udp_remote_endpoint.address();
                    if (addr.is_v4()) {
                        for (auto b : addr.to_v4().to_bytes()) p << b;
                    } else if (addr.is_v6() && addr.to_v6().is_v4_mapped()) {
                        for (auto b : addr.to_v6().to_v4().to_bytes()) p << b;
                    } else {
                        for (auto b : addr.to_v6().to_bytes()) p << b;
                    }
                    error_code error;
                    udp_socket.send_to(buffer(p), udp_remote_endpoint, 0, error);
                    if (error) return;
                    break;
                }
            }
        }
        read();
    });
}

void server::on_user_join(user* user, string room_id) {
    if (multiroom) {
        if (room_id == "") room_id = get_random_room_id();
    } else {
        room_id = "";
    }

    if (rooms.find(room_id) == rooms.end()) {
        rooms[room_id] = make_shared<room>(room_id, this, user->rom);
        log("[" + room_id + "] " + user->name + " created room");
        log("[" + room_id + "] " + user->name + " set game to " + user->rom.to_string());
        log_room_list();
    }

    rooms[room_id]->on_user_join(user);
}

void server::on_user_quit(user* user) {
    users.erase(user);
}

void server::on_room_close(room* room) {
    auto id = room->get_id();
    auto age = static_cast<int>(timestamp() - room->creation_timestamp);
    if (rooms.erase(id)) {
        log("[" + id + "] Room destroyed after " + to_string(age / 60) + "m" + to_string(age % 60) + "s");
        log_room_list();
    }
}

void server::on_tick() {
    for (auto& e : rooms) {
        e.second->on_ping_tick();
    }

    if (tick_count % 60 == 0) {
        for (auto& u : users) {
            u.second->send_keepalive();
        }
    }

    tick_count++;
    timer.expires_after(500ms);
    timer.async_wait([=](const error_code& error) { if (!error) on_tick(); });
}

string server::get_random_room_id() {
    static constexpr char ALPHABET[] = "123456789abcdefghjkmnpqrstuvwxyz";
    static uniform_int_distribution<size_t> dist(0, strlen(ALPHABET) - 1);
    static random_device rd;

    string result;
    result.resize(4);
    do {
        for (char& c : result) {
            c = ALPHABET[dist(rd)];
        }
    } while (rooms.find(result) != rooms.end());

    return result;
}

void server::log_room_list() {
    string room_list;
    if (rooms.empty()) {
        log("Room Count: 0");
    } else {
        for (auto& e : rooms) {
            if (room_list.empty()) {
                room_list = e.first;
            } else {
                room_list += ", " + e.first;
            }
            if (!e.second->started) {
                room_list += "*";
            }
        }
        log("Room Count: " + to_string(rooms.size()) + " (" + room_list + ")");
    }
}

#ifdef __GNUC__
#if !defined(__MINGW32__) && !defined(__MINGW64__)
void handle(int sig) {
    log(cerr, "SIGNAL: " + to_string(sig));
    print_stack_trace();
    exit(1);
}
#endif
#endif

int main(int argc, char* argv[]) {
#ifdef __GNUC__
#if !defined(__MINGW32__) && !defined(__MINGW64__)
    signal(SIGSEGV, handle);
#endif
#endif
    log(APP_NAME_AND_VERSION);

    try {
        uint16_t port = argc >= 2 ? stoi(argv[1]) : 6400;
        io_service service;
        server my_server(service, true);
        my_server.open(port);
        service.run();
    } catch (const exception& e) {
        log(cerr, e.what());
        return 1;
    } catch (const error_code& e) {
        log(cerr, e.message());
        return 1;
    }

    return 0;
}
