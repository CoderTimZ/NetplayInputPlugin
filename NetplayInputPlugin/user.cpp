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
        my_room = nullptr;
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

void user::on_receive(packet& p, bool udp) {
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
            dynamic_cast<user_info&>(*this) = p.read<user_info>();
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
            if (udp) can_recv_udp = true;
            packet pong;
            pong << PONG;
            while (p.available()) {
                pong << p.read<uint8_t>();
            }
            if (udp) {
                send_udp(pong);
            } else {
                send(pong);
            }
            break;
        }

        case PONG: {
            if (udp && !can_send_udp) {
                can_send_udp = true;
                tcp_socket->set_option(ip::tcp::no_delay(false));
            }
            auto time = p.read<double>();
            if (time <= last_pong) break;
            last_pong = time;
            latency = timestamp() - time;
            latency_history.push_back(latency);
            while (latency_history.size() > 7) latency_history.pop_front();
            break;
        }

        case NAME: {
            string old_name = name;
            p.read(name);
            trim(name);
            log("[" + my_room->get_id() + "] " + old_name + " is now " + name);
            for (auto& u : my_room->user_list) {
                if (u->id == id) continue;
                u->send_name(id, name);
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
                log("[" + my_room->get_id() + "] " + name + " set their lag to " + to_string((int)lag));
                send_info(name + " set the lag to " + to_string((int)lag));
            }
            if (room_lag) {
                my_room->set_lag(lag, this);
                log("[" + my_room->get_id() + "] " + name + " set the room lag to " + to_string((int)lag));
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
                log("[" + my_room->get_id() + "] " + name + " enabled autolag");
            } else {
                my_room->send_info("Automatic lag is disabled");
                log("[" + my_room->get_id() + "] " + name + " disabled autolag");
            }
            break;
        }

        case CONTROLLERS: {
            for (auto& c : controllers) {
                p >> c;
            }
            if (!my_room->started) {
                my_room->update_controller_map();
            }
            my_room->send_controllers();
            log("[" + my_room->get_id() + "] " + name + " configured their controllers");
            break;
        }

        case START: {
            log("[" + my_room->get_id() + "] " + name + " started the game");
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
                log("[" + my_room->get_id() + "] " + name + " enabled golf mode");
            } else {
                log("[" + my_room->get_id() + "] " + name + " disabled golf mode");
            }
            break;
        }

        case INPUT_MAP: {
            map = p.read<input_map>();
            manual_map = true;
            packet p;
            p << INPUT_MAP << id << map;
            for (auto& u : my_room->user_list) {
                if (u->id == id) continue;
                u->send(p);
            }
            my_room->send_info(name + " remapped their controller input");
            log("[" + my_room->get_id() + "] " + name + " remapped their controller input");
            break;
        }

        case INPUT_DATA: {
            auto user = my_room->user_map.at(p.read_var<uint32_t>());
            if (!user) break;
            auto i = p.read_var<uint32_t>();
            auto pin = p.read_rle().transpose(input_data::SIZE, 0);
            while (pin.available()) {
                if (user->add_input_history(i++, pin.read<input_data>())) {
                    for (auto& u : my_room->user_list) {
                        if (u->id == id) continue;
                        u->write_input_from(user);
                    }
                }
            }
            break;
        }

        case INPUT_UPDATE: {
            auto authority_user = my_room->user_map.at(authority);
            if (!authority_user) break;
            authority_user->send_input_update(id, p.read<input_data>());
            break;
        }

        case FRAME: {
            record_input_timestamp();
            break;
        }

        case REQUEST_AUTHORITY: {
            auto user = my_room->user_map.at(p.read<uint32_t>());
            auto authority = my_room->user_map.at(p.read<uint32_t>());
            if (!user || !authority) break;
            for (auto& u : my_room->user_list) {
                if (u->id == id) continue;
                u->send_request_authority(user->id, authority->id);
            }
            break;
        }

        case DELEGATE_AUTHORITY: {
            auto user = my_room->user_map.at(p.read<uint32_t>());
            auto authority = my_room->user_map.at(p.read<uint32_t>());
            if (!user || !authority) break;
            user->authority = authority->id;
            for (auto& u : my_room->user_list) {
                if (u->id == id) continue;
                u->send_delegate_authority(user->id, user->authority);
            }
            for (auto& u : my_room->user_list) {
                u->has_authority = false;
            }
            for (auto& u : my_room->user_list) {
                auto auth = my_room->user_map.at(u->authority);
                if (auth) auth->has_authority = true;
            }
            break;
        }
    }
}

void user::set_lag(uint8_t lag, user* source) {
    this->lag = lag;
    packet p;
    p << LAG << lag << (source ? source->id : 0xFFFFFFFF) << id;
    for (auto& u : my_room->user_list) {
        u->send(p);
    }
}

void user::send_keepalive() {
    send(packet());
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
            p << true << dynamic_cast<user_info&>(*u);
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
    send_udp(p);
    if (!can_send_udp) {
        send(p);
    }
}

void user::write_input_from(user* user) {
    packet pin;
    for (auto& e : user->input_history) {
        pin << e;
    }

    if (can_send_udp) {
        packet p;
        p << INPUT_DATA;
        p.write_var(user->id);
        p.write_var(user->input_id - user->input_history.size());
        p.write_rle(pin.transpose(0, input_data::SIZE));
        send_udp(p, false);
    }

    packet p;
    p << INPUT_DATA;
    p.write_var(user->id);
    p.write_var(user->input_id - 1);
    p.write_rle(pin.reset() << user->input_history.back());
    send(p, false);

    for (auto& u : my_room->user_list) {
        if (u->authority == id) continue;
        if (u->input_id < user->input_id) return;
    }
    
    flush_all();
}

void user::record_input_timestamp() {
    auto now = timestamp();
    input_timestamps.push_back(now);
    while (input_timestamps.front() < now - 2.0) {
        input_timestamps.pop_front();
    }
}

void user::send_input_update(uint32_t id, const input_data& input) {
    if (can_send_udp) {
        send_udp(packet() << INPUT_UPDATE << id << input);
    } else {
        send(packet() << INPUT_UPDATE << id << input);
    }
}

void user::send_request_authority(uint32_t user_id, uint32_t authority_id) {
    send(packet() << REQUEST_AUTHORITY << user_id << authority_id);
}

void user::send_delegate_authority(uint32_t user_id, uint32_t authority_id) {
    send(packet() << DELEGATE_AUTHORITY << user_id << authority_id);
}
