#include "stdafx.h"

#include "user.h"
#include "common.h"
#include "util.h"

using namespace std;
using namespace asio;

uint32_t user::next_id = 0;

user::user(shared_ptr<io_service> io_s, shared_ptr<server> my_server) : connection(io_s), my_server(my_server), id(next_id++) { }

void user::set_room(room_ptr my_room) {
    this->my_room = my_room;

    send(packet() << PATH << ("/" + my_room->get_id()));
}

bool user::joined() {
    return (bool)my_room;
}

void user::handle_error(const error_code& error) {
    if (joined()) {
        log("(" + my_room->get_id() + ") " + name + " (" + address + ") disconnected");
        my_room->on_user_quit(shared_from_this());
    } else {
        log(address + " disconnected");
    }
}

uint32_t user::get_id() const {
    return id;
}

bool user::is_player() const {
    return my_controller_map.count() > 0;
}

const array<controller, MAX_PLAYERS>& user::get_controllers() const {
    return controllers;
}

const string& user::get_name() const {
    return name;
}

double user::get_latency() const {
    return latency_history.empty() ? NAN : latency_history.front();
}

double user::get_median_latency() const {
    if (latency_history.empty()) return NAN;
    vector<double> lat(latency_history.begin(), latency_history.end());
    sort(lat.begin(), lat.end());
    return lat[lat.size() / 2];
}

double user::get_fps() {
    if (frame_history.empty() || frame_history.front() == frame_history.back()) {
        return NAN;
    } else {
        return (frame_history.size() - 1) / (frame_history.back() - frame_history.front());
    }
}

void user::process_packet() {
    auto self(shared_from_this());
    read([=](packet& p) {
        if (p.size() == 0) return self->process_packet();

        switch (p.read<uint8_t>()) {
            case JOIN: {
                if (joined()) break;
                auto protocol_version = p.read<uint32_t>();
                if (protocol_version != PROTOCOL_VERSION) {
                    return close();
                }
                string room = p.read();
                if (!room.empty() && room[0] == '/') {
                    room = room.substr(1);
                }
                p.read(self->name);
                log(address + " is " + self->name);
                for (auto& c : controllers) {
                    p >> c.plugin >> c.present >> c.raw_data;
                }
                my_server->on_user_join(shared_from_this(), room);
                break;
            }

            case PING: {
                packet reply;
                reply << PONG;
                while (p.bytes_remaining()) reply << p.read<uint8_t>();
                send(reply);
                break;
            }

            case PONG: {
                latency_history.push_back(timestamp() - p.read<double>());
                while (latency_history.size() > 5) latency_history.pop_front();
                break;
            }

            case CONTROLLERS: {
                if (!joined()) break;
                if (my_room->started) break;
                for (auto& c : controllers) {
                    p >> c.plugin >> c.present >> c.raw_data;
                }
                my_room->update_controllers();
                break;
            }

            case NAME: {
                if (!joined()) break;
                string old_name = self->name;
                p.read(self->name);
                log("(" + my_room->get_id() + ") " + old_name + " is now " + self->name);
                for (auto& u : my_room->users) {
                    u->send_name(id, name);
                }
                break;
            }

            case MESSAGE: {
                if (!joined()) break;
                string message = p.read();
                log("(" + my_room->get_id() + ") " + self->name + ": " + message);
                auto self(shared_from_this());
                for (auto& u : my_room->users) {
                    if (u == self) continue;
                    u->send_message(get_id(), message);
                }
                break;
            }

            case LAG: {
                if (!joined()) break;
                auto lag = p.read<uint8_t>();
                log("(" + my_room->get_id() + ") " + self->name + " set the lag to " + to_string(lag));
                my_room->send_lag(id, lag);
                break;
            }

            case AUTOLAG: {
                if (!joined()) break;
                my_room->autolag = !my_room->autolag;
                if (my_room->autolag) {
                    log("(" + my_room->get_id() + ") " + self->name + " enabled autolag");
                    my_room->send_status("Automatic Lag is enabled");
                } else {
                    log("(" + my_room->get_id() + ") " + self->name + " disabled autolag");
                    my_room->send_status("Automatic Lag is disabled");
                }
                break;
            }

            case START: {
                if (!joined()) break;
                log("(" + my_room->get_id() + ") " + self->name + " started the game");
                my_room->on_game_start();
                break;
            }

            case INPUT_DATA: {
                if (!joined()) break;
                auto port = p.read<uint8_t>();
                input input{ p.read<uint32_t>() };

                for (auto& u : my_room->users) {
                    if (u->get_id() == id) continue;
                    u->send_input(port, input);
                }
                break;
            }

            case FRAME: {
                auto ts = timestamp();
                frame_history.push_back(ts);
                while (frame_history.front() <= ts - 5.0) {
                    frame_history.pop_front();
                }
                break;
            }
        }

        self->process_packet();
    });
}

void user::send_protocol_version() {
    send(packet() << VERSION << PROTOCOL_VERSION);
}

void user::send_join(uint32_t user_id, const string& name) {
    send(packet() << JOIN << user_id << name);
}

void user::send_netplay_controllers(const array<controller, MAX_PLAYERS>& controllers) {
    packet p;
    p << CONTROLLERS;
    p << (int32_t)-1;
    for (auto& c : controllers) {
        p << c.plugin << c.present << c.raw_data;
    }
    for (auto netplay_controller : my_controller_map.netplay_to_local) {
        p << netplay_controller;
    }
    send(p);
}

void user::send_start_game() {
    send(packet() << START);
}

void user::send_name(uint32_t user_id, const string& name) {
    send(packet() << NAME << user_id << name);
}

void user::send_ping() {
    send(packet() << PING << timestamp());
}

void user::send_quit(uint32_t id) {
    send(packet() << QUIT << id);
}

void user::send_message(int32_t id, const string& message) {
    send(packet() << MESSAGE << id << message);
}

void user::send_status(const string& message) {
    send_message(STATUS_MESSAGE, message);
}

void user::send_error(const string& message) {
    send_message(ERROR_MESSAGE, message);
}

void user::send_lag(uint8_t lag) {
    send(packet() << LAG << lag);
}

void user::send_input(uint8_t port, input input) {
    send(packet() << INPUT_DATA << port << input.value, false);

    pending_input_data_packets++;
    if (pending_input_data_packets >= my_room->player_count() - my_controller_map.count()) {
        flush();
        pending_input_data_packets = 0;
    }
}
