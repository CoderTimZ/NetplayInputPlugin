#include "stdafx.h"

#include "user.h"
#include "client_server_common.h"
#include "util.h"

using namespace std;
using namespace asio;

uint32_t user::next_id = 0;

user::user(shared_ptr<ip::tcp::socket> socket, shared_ptr<server> my_server) : connection(socket), my_server(my_server), id(next_id++) { }

user::~user() {

}

void user::set_room(room_ptr my_room) {
    this->my_room = my_room;

    send(packet() << PATH << ("/" + my_room->get_id()));
}

void user::handle_error(const error_code& error) {
    if (my_room) {
        my_room->on_user_quit(shared_from_this());
    }
}

void user::close() {
    error_code error;
    socket->shutdown(ip::tcp::socket::shutdown_both, error);
    socket->close(error);
}

uint32_t user::get_id() const {
    return id;
}

bool user::is_player() const {
    return my_controller_map.local_count() > 0;
}

const array<controller, MAX_PLAYERS>& user::get_controllers() const {
    return controllers;
}

const string& user::get_name() const {
    return name;
}

int32_t user::get_latency() const {
    return latency_history.empty() ? -1 : latency_history.front();
}

int32_t user::get_minimum_latency() const {
    auto result = std::min_element(std::begin(latency_history), std::end(latency_history));
    return result == std::end(latency_history) ? -1 : *result;
}

size_t user::get_fps() {
    return frame_history.size();
}

void user::process_packet() {
    auto self(shared_from_this());
    read([=](packet& p) {
        if (p.size() == 0) return self->process_packet();

        auto packet_type = p.read<uint8_t>();

        if (!joined && packet_type != JOIN) {
            return close();
        }

        switch (packet_type) {
            case JOIN: {
                if (joined) break;
                auto protocol_version = p.read<uint32_t>();
                if (protocol_version != PROTOCOL_VERSION) {
                    return close();
                }
                string room = p.read();
                if (!room.empty() && room[0] == '/') {
                    room = room.substr(1);
                }
                p.read(self->name);
                for (auto& c : controllers) {
                    p >> c.plugin >> c.present >> c.raw_data;
                }
                joined = true;
                my_server->on_user_join(shared_from_this(), room);
                break;
            }

            case PONG: {
                auto timestamp = p.read<uint64_t>();
                latency_history.push_back((uint32_t)(server::time() - timestamp) / 2);
                while (latency_history.size() > 4) latency_history.pop_front();
                break;
            }

            case CONTROLLERS: {
                if (my_room->started) break;
                for (auto& c : controllers) {
                    p >> c.plugin >> c.present >> c.raw_data;
                }
                my_room->update_controllers();
                break;
            }

            case NAME: {
                p.read(self->name);
                for (auto& u : my_room->users) {
                    u->send_name(id, name);
                }
                break;
            }

            case MESSAGE: {
                string message = p.read();
                auto self(shared_from_this());
                for (auto& u : my_room->users) {
                    if (u == self) continue;
                    u->send_message(get_id(), message);
                }
                break;
            }

            case LAG: {
                auto lag = p.read<uint8_t>();
                my_room->send_lag(id, lag);
                break;
            }

            case AUTOLAG: {
                my_room->autolag = !my_room->autolag;
                if (my_room->autolag) {
                    my_room->send_status("Automatic Lag is enabled");
                } else {
                    my_room->send_status("Automatic Lag is disabled");
                }
                break;
            }

            case START: {
                my_room->on_game_start();
                break;
            }

            case INPUT_DATA: {
                auto port = p.read<uint8_t>();
                input input{ p.read<uint32_t>() };

                for (auto& u : my_room->users) {
                    if (u->get_id() == id) continue;
                    u->send_input(port, input);
                }
                break;
            }

            case FRAME: {
                auto time = server::time();
                frame_history.push_back(time);
                while (frame_history.front() <= time - 1000) {
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
    for (auto netplay_controller : my_controller_map.local_to_netplay) {
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

void user::send_ping(uint64_t time) {
    send(packet() << PING << time);
}

void user::send_quit(uint32_t id) {
    send(packet() << QUIT << id);
}

void user::send_message(int32_t id, const string& message) {
    send(packet() << MESSAGE << id << message);
}

void user::send_lag(uint8_t lag) {
    send(packet() << LAG << lag);
}

void user::send_input(uint8_t port, input input) {
    send(packet() << INPUT_DATA << port << input.value, false);

    pending_input_data_packets++;
    if (pending_input_data_packets >= my_room->player_count() - my_controller_map.local_count()) {
        flush();
        pending_input_data_packets = 0;
    }
}
