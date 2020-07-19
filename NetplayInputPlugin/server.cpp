#include "stdafx.h"

#include "server.h"
#include "room.h"
#include "user.h"
#include "version.h"

using namespace std;
using namespace asio;

server::server(io_service& service, bool multiroom) :
     service(&service), multiroom(multiroom), acceptor(service), udp_socket(service), timer(service) { }

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
    error_code error;

    if (acceptor.is_open()) {
        acceptor.close(error);
    }

    if (udp_socket.is_open()) {
        udp_socket.close();
    }

    timer.cancel(error);

    unordered_map<string, shared_ptr<room>> r;
    r.swap(rooms);
    for (auto& e : r) {
        e.second->close();
    }
}

void server::accept() {
    auto u = make_shared<user>(this);
    acceptor.async_accept(*(u->tcp_socket), [=](error_code error) {
        if (error) return;

        u->tcp_socket->set_option(ip::tcp::no_delay(), error);
        if (error) return;

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
            switch (p.read<packet_type>()) {
                case PING: {
                    packet pong;
                    pong << PONG << PROTOCOL_VERSION;
                    while (p.available()) {
                        pong << p.read<uint8_t>();
                    }
                    error_code error;
                    udp_socket.send_to(buffer(pong), udp_remote_endpoint, 0, error);
                    if (error) return;
                    log(endpoint_to_string(udp_remote_endpoint) + " pinged the server");
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

    //log(user->info.name + " (" + endpoint_to_string(user->tcp_socket->remote_endpoint()) + ") connected");

    if (rooms.find(room_id) == rooms.end()) {
        rooms[room_id] = make_shared<room>(room_id, this, user->info.rom);
        log("[" + room_id + "] " + user->info.name + " created room");
        log("[" + room_id + "] " + user->info.name + " set game to " + user->info.rom.to_string());
        log("Room count: " + to_string(rooms.size()));
    }

    rooms[room_id]->on_user_join(user);
}

void server::on_user_quit(user* user) {
    //log(user->info.name + " (" + endpoint_to_string(user->tcp_socket->remote_endpoint()) + ") disconnected");
    users.erase(user);
}

void server::on_room_close(room* room) {
    auto id = room->get_id();
    auto age = static_cast<int>(timestamp() - room->creation_timestamp);
    if (rooms.erase(id)) {
        log("[" + id + "] Room destroyed after " + to_string(age / 60) + "m" + to_string(age % 60) + "s");
        log("Room count: " + to_string(rooms.size()));
    }
}

void server::on_tick() {
    for (auto& e : rooms) {
        e.second->on_ping_tick();
    }

    timer.expires_from_now(500ms);
    timer.async_wait([=](const error_code& error) { if (!error) on_tick(); });
}

string server::get_random_room_id() {
    static constexpr char ALPHABET[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
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

#ifdef __GNUC__
void handle(int sig) {
    print_stack_trace();
    exit(1);
}
#endif

int main(int argc, char* argv[]) {
#ifdef __GNUC__
    signal(SIGSEGV, handle);
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
