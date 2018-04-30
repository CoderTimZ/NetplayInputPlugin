#include "stdafx.h"

#include "session.h"
#include "client_server_common.h"
#include "util.h"

using namespace std;
using namespace asio;

session::session(shared_ptr<server> my_server, uint32_t id) : connection(my_server->io_s), my_server(my_server), id(id) { }

void session::handle_error(const error_code& error) {
    if (error == error::operation_aborted) return;

    my_server->on_session_quit(shared_from_this());
}

void session::close() {
    error_code error;
    socket.shutdown(ip::tcp::socket::shutdown_both, error);
    socket.close(error);
}

uint32_t session::get_id() const {
    return id;
}

bool session::is_player() const {
    return my_controller_map.local_count() > 0;
}

const array<controller, MAX_PLAYERS>& session::get_controllers() const {
    return controllers;
}

const string& session::get_name() const {
    return name;
}

int32_t session::get_latency() const {
    return latency_history.empty() ? -1 : latency_history.front();
}

int32_t session::get_minimum_latency() const {
    auto result = std::min_element(std::begin(latency_history), std::end(latency_history));
    return result == std::end(latency_history) ? -1 : *result;
}

uint32_t session::get_fps() {
    return frame_history.size();
}

void session::process_packet() {
    auto self(shared_from_this());
    read([=](packet& p) {
        if (p.size() == 0) return self->process_packet();

        auto packet_type = p.read<uint8_t>();

        if (!joined && packet_type != JOIN) {
            return close();
        }

        switch (packet_type) {
            case JOIN: {
                auto protocol_version = p.read<uint32_t>();
                if (protocol_version != PROTOCOL_VERSION) {
                    return close();
                }
                auto name_length = p.read<uint8_t>();
                string name(name_length, ' ');
                p.read(name);
                self->name = name;
                for (auto& c : controllers) {
                    p >> c.plugin >> c.present >> c.raw_data;
                }
                joined = true;
                my_server->on_session_joined(shared_from_this());
                break;
            }

            case PONG: {
                auto timestamp = p.read<uint64_t>();
                latency_history.push_back((uint32_t)(my_server->time() - timestamp) / 2);
                while (latency_history.size() > 4) latency_history.pop_front();
                break;
            }

            case CONTROLLERS: {
                if (my_server->game_started) break;
                for (auto& c : controllers) {
                    p >> c.plugin >> c.present >> c.raw_data;
                }
                my_server->update_controllers();
                break;
            }

            case NAME: {
                auto name_length = p.read<uint8_t>();
                string name(name_length, ' ');
                p.read(name);
                my_server->send_name(id, name);
                self->name = name;
                break;
            }

            case MESSAGE: {
                auto message_length = p.read<uint16_t>();
                string message(message_length, ' ');
                p.read(message);
                my_server->send_message(id, message);
                break;
            }

            case LAG: {
                auto lag = p.read<uint8_t>();
                my_server->send_lag(id, lag);
                break;
            }

            case AUTOLAG: {
                my_server->autolag = !my_server->autolag;
                if (my_server->autolag) {
                    my_server->send_message(-1, "Automatic Lag is enabled");
                } else {
                    my_server->send_message(-1, "Automatic Lag is disabled");
                }
                break;
            }

            case START: {
                my_server->send_start_game();
                break;
            }

            case INPUT_DATA: {
                auto port = p.read<uint8_t>();
                input input;
                input.value = p.read<uint32_t>();

                my_server->send_input(id, port, input);
                break;
            }

            case FRAME: {
                auto time = my_server->time();
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

void session::send_protocol_version() {
    send(packet() << VERSION << PROTOCOL_VERSION);
}

void session::send_join(uint32_t user_id, const string& name) {
    packet p;
    p << JOIN;
    p << user_id;
    p << (uint8_t)name.size();
    p << name;

    send(p);
}

void session::send_netplay_controllers(const array<controller, MAX_PLAYERS>& controllers) {
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

void session::send_start_game() {
    send(packet() << START);
}

void session::send_name(uint32_t id, const string& name) {
    packet p;
    p << NAME;
    p << id;
    p << (uint8_t)name.size();
    p << name;

    send(p);
}

void session::send_ping(uint64_t time) {
    send(packet() << PING << time);
}

void session::send_quit(uint32_t id) {
    send(packet() << QUIT << id);
}

void session::send_message(int32_t id, const string& message) {
    send(packet() << MESSAGE << id << (uint16_t)message.size() << message);
}

void session::send_lag(uint8_t lag) {
    send(packet() << LAG << lag);
}

void session::send_input(uint8_t port, input input) {
    send(packet() << INPUT_DATA << port << input.value, false);

    pending_input_data_packets++;
    if (pending_input_data_packets >= my_server->player_count() - my_controller_map.local_count()) {
        flush();
        pending_input_data_packets = 0;
    }
}
