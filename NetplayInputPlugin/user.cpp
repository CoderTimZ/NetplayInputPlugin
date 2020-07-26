#include "stdafx.h"

#include "user.h"
#include "common.h"
#include "util.h"

using namespace std;
using namespace asio;

user::user(server* server) :
    connection(*server->service), my_server(server) { }

void user::set_room(room* room) {
    this->my_room = room;

    send(packet() << PATH << ("/" + room->get_id()));
}

void user::on_error(const error_code& error) {
    if (my_room) {
        my_room->on_user_quit(this);
    }
    my_server->on_user_quit(this);
}

double user::get_median_latency() const {
    if (latency_history.empty()) return nan("");
    vector<double> lat(latency_history.begin(), latency_history.end());
    sort(lat.begin(), lat.end());
    return lat[lat.size() / 2];
}

double user::get_input_rate() {
    if (input_timestamps.empty() || input_timestamps.front() == input_timestamps.back()) {
        return nan("");
    } else {
        return (input_timestamps.size() - 1) / (input_timestamps.back() - input_timestamps.front());
    }
}

void user::on_receive(packet& p, bool reliable) {
    auto type = p.read<packet_type>();
    if (type != JOIN && !my_room) {
        return;
    }

    switch (type) {
        case JOIN: {
            if (my_room) break;
            auto protocol_version = p.read<uint32_t>();
            if (protocol_version != PROTOCOL_VERSION) {
                return close();
            }
            auto room = p.read<string>();
            trim(room);
            if (!room.empty() && room[0] == '/') {
                room = room.substr(1);
            }
            info = p.read<user_info>();
            auto udp_port = p.read<uint16_t>();
            if (udp_port) {
                auto local_endpoint = ip::udp::endpoint(tcp_socket->local_endpoint().address(), 0);
                auto remote_endpoint = ip::udp::endpoint(tcp_socket->remote_endpoint().address(), udp_port);
                udp_socket->open(local_endpoint.protocol());
                udp_socket->bind(local_endpoint);
                udp_socket->connect(remote_endpoint);
                receive_udp_packet();
            } else {
                udp_socket.reset();
            }
            my_server->on_user_join(this, room);
            break;
        }

        case PING: {
            can_recv_udp |= !reliable;
            packet pong;
            pong << PONG << reliable;
            while (p.available()) {
                pong << p.read<uint8_t>();
            }
            send(pong, !can_send_udp);
            break;
        }

        case PONG: {
            can_send_udp |= !p.read<bool>();
            auto time = p.read<double>();
            if (time <= last_pong) break;
            last_pong = time;
            info.latency = timestamp() - time;
            latency_history.push_back(info.latency);
            while (latency_history.size() > 7) latency_history.pop_front();
            break;
        }

        case NAME: {
            string old_name = info.name;
            p.read(info.name);
            trim(info.name);
            log("[" + my_room->get_id() + "] " + old_name + " is now " + info.name);
            for (auto& u : my_room->user_list) {
                if (u->id == id) continue;
                u->send_name(id, info.name);
            }
            break;
        }

        case MESSAGE: {
            auto message = p.read<string>();
            for (auto& u : my_room->user_list) {
                if (u->id == id) continue;
                u->send_message(id, message);
            }
            break;
        }

        case LAG: {
            auto lag = p.read<uint8_t>();
            auto source_lag = p.read<bool>();
            auto room_lag = p.read<bool>();
            if (source_lag) {
                set_lag(lag, this);
            }
            if (room_lag) {
                my_room->set_lag(lag, this);
            }
            break;
        }

        case AUTOLAG: {
            auto value = p.read<int8_t>();
            if (value == (int8_t)my_room->autolag) break;

            if (value == 0) {
                my_room->autolag = false;
            } else if (value == 1) {
                my_room->autolag = true;
            } else {
                my_room->autolag = !my_room->autolag;
            }
            if (my_room->autolag) {
                my_room->send_info("Automatic lag is enabled");
            } else {
                my_room->send_info("Automatic lag is disabled");
            }
            break;
        }

        case CONTROLLERS: {
            for (auto& c : info.controllers) {
                p >> c;
            }
            if (!my_room->started) {
                my_room->update_controller_map();
            }
            my_room->send_controllers();
            break;
        }

        case START: {
            log("[" + my_room->get_id() + "] " + info.name + " started the game");
            my_room->on_game_start();
            break;
        }

        case GOLF: {
            auto golf = p.read<bool>();
            if (my_room->golf == golf) break;
            my_room->golf = golf;
            for (auto& u : my_room->user_list) {
                if (u->id == id) continue;
                u->send(p);
            }
            if (golf) {
                my_room->autolag = false;
                my_room->set_lag(0, nullptr);
                for (auto& u : my_room->user_list) {
                    u->set_input_authority(HOST);
                }
                my_room->send_info("==> Please DISABLE your emulator's frame rate limit <==");
            }
            break;
        }

        case INPUT_MAP: {
            info.map = p.read<input_map>();
            info.manual_map = true;
            packet p;
            p << INPUT_MAP << id << info.map;
            for (auto& u : my_room->user_list) {
                if (u->id == id) continue;
                u->send(p);
            }
            break;
        }

        case INPUT_AUTHORITY: {
            if (!set_input_authority(p.read<application>(), CLIENT)) break;
            if (my_room->golf && info.input_authority == CLIENT) {
                for (auto& u : my_room->user_list) {
                    if (u->id == id) continue;
                    u->set_input_authority(HOST);
                }
            }
            break;
        }

        case INPUT_DATA: {
            switch (p.read<application>()) {
                case CLIENT: {
                    auto i = p.read_var<uint32_t>();
                    auto pin = p.read_rle().transpose(input_data::SIZE, 0);
                    while (pin.available()) {
                        if (info.add_input_history(i++, pin.read<input_data>())) {
                            record_input_timestamp();
                            for (auto& u : my_room->user_list) {
                                if (u->id == id) continue;
                                u->write_input_from(this);
                            }
                            my_room->on_input_from(this);
                        }
                    }
                    break;
                }

                case HOST: {
                    hia_input = p.read<input_data>();
                    record_input_timestamp();
                    break;
                }
            }
            break;
        }

        case HIA_RATE: {
            my_room->hia_rate = max(5u, min(300u, p.read<uint32_t>()));
            break;
        }
    }
}

bool user::set_input_authority(application authority, application initiator) {
    if (authority == info.input_authority) return false;

    if (authority == CLIENT || initiator == CLIENT) {
        info.input_authority = authority;
        hia_input = input_data();
        size_t cia_count = 0;
        for (auto& u : my_room->user_list) {
            if (u->info.input_authority == CLIENT) cia_count++;
            if (u->id == id) continue;
            u->send(packet() << INPUT_AUTHORITY << id << authority);
        }
        if (authority == CLIENT && cia_count == my_room->user_list.size() && !my_room->golf) {
            my_room->send_info("==> Please ENABLE your emulator's frame rate limit <==");
        } else if (authority == HOST && cia_count == my_room->user_list.size() - 1 && !my_room->golf) {
            my_room->send_info("==> Please DISABLE your emulator's frame rate limit <==");
        }
    }

    if (authority == CLIENT || initiator == HOST) {
        send(packet() << INPUT_AUTHORITY << id << authority);
    }

    return true;
}

void user::set_lag(uint8_t lag, user* source) {
    info.lag = lag;
    packet p;
    p << LAG << lag << (source ? source->id : 0xFFFFFFFF) << id;
    for (auto& u : my_room->user_list) {
        u->send(p);
    }
}

void user::send_protocol_version() {
    send(packet() << VERSION << PROTOCOL_VERSION);
}

void user::send_accept() {
    uint16_t udp_port = 0;
    if (udp_socket && udp_socket->is_open()) {
        udp_port = udp_socket->local_endpoint().port();
    }
    packet p;
    p << ACCEPT << udp_port;
    for (auto& u : my_room->user_map) {
        if (u) {
            p << true << u->info;
        } else {
            p << false;
        }
    }
    send(p);
}

void user::send_join(const user_info& info) {
    send(packet() << JOIN << info);
}

void user::send_start_game() {
    send(packet() << START);
}

void user::send_name(uint32_t user_id, const string& name) {
    send(packet() << NAME << user_id << name);
}

void user::send_quit(uint32_t id) {
    send(packet() << QUIT << id);
}

void user::send_message(uint32_t id, const string& message) {
    send(packet() << MESSAGE << id << message);
}

void user::send_info(const string& message) {
    send_message(INFO_MSG, message);
}

void user::send_error(const string& message) {
    send_message(ERROR_MSG, message);
}

void user::send_ping() {
    packet p;
    p << PING << timestamp();
    send(p, false);
    if (!can_send_udp) {
        send(p, true);
    }
}

void user::write_input_from(user* from) {
    packet input_packet;
    for (auto& e : from->info.input_history) {
        input_packet << e;
    }

    if (can_send_udp) {
        if (udp_input_buffer.empty()) {
            udp_input_buffer << INPUT_DATA;
        }
        udp_input_buffer.write_var(from->id);
        udp_input_buffer.write_var(from->info.input_id - from->info.input_history.size());
        udp_input_buffer.write_rle(input_packet.transpose(0, input_data::SIZE));
    }

    packet p;
    p << INPUT_DATA;
    p.write_var(from->id);
    p.write_var(from->info.input_id - 1);
    p.write_rle(input_packet.reset() << from->info.input_history.back());
    send(p, false);
}

void user::flush_input() {
    if (!udp_input_buffer.empty()) {
        send(udp_input_buffer, false);
        udp_input_buffer.reset();
    }

    flush();
}

void user::record_input_timestamp() {
    auto now = timestamp();
    input_timestamps.push_back(now);
    while (input_timestamps.front() < now - 2.0) {
        input_timestamps.pop_front();
    }
}