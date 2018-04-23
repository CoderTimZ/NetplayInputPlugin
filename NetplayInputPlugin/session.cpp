#include "server.h"
#include "session.h"
#include "client_server_common.h"
#include "util.h"

using namespace std;
using namespace boost::asio;

session::session(server& my_server, uint32_t id)
    : socket(my_server.io_s), my_server(my_server), id(id), controllers(MAX_PLAYERS) { }

void session::handle_error(const boost::system::error_code& error) {
    if (error == error::operation_aborted) {
        return;
    }

    my_server.remove_session(id);
}

void session::stop() {
    boost::system::error_code error;
    socket.shutdown(ip::tcp::socket::shutdown_both, error);
    socket.close(error);
}

uint32_t session::get_id() const {
    return id;
}

bool session::is_player() {
    return in_buttons.size() > 0;
}

const vector<CONTROL>& session::get_controllers() const {
    return controllers;
}

const wstring& session::get_name() const {
    return name;
}

int32_t session::get_latency() const {
    return latency_history.empty() ? -1 : latency_history.front();
}

int32_t session::get_average_latency() const {
    auto size = latency_history.size();
    if (size == 0) return -1;
    int32_t sum = 0;
    for (auto latency : latency_history) {
        sum += latency;
    }
    return sum / size;
}

void session::send_controller_range(uint8_t player_index, uint8_t player_count) {
    this->player_index = player_index;
    in_buttons.resize(player_count);

    send(packet() << PLAYER_RANGE << player_index << player_count);
}

void session::send_controllers(const vector<CONTROL>& controllers) {
    assert(controllers.size() == MAX_PLAYERS);

    int total_count = 0;
    for (size_t i = 0; i < controllers.size(); i++) {
        if (controllers[i].Present) {
            total_count++;
        }
    }

    output_buttons.resize(total_count);

    send(packet() << CONTROLLERS << controllers);
}

void session::send_start_game() {
    send(packet() << START_GAME);
}

void session::send_name(uint32_t id, const wstring& name) {
    packet p;
    p << NAME;
    p << id;
    p << (uint8_t) name.size();
    p << name;

    send(p);
}

void session::send_ping() {
    send(packet() << PING << get_time());
}

void session::send_departure(uint32_t id) {
    send(packet() << QUIT << id);
}

void session::send_message(int32_t id, const wstring& message) {
    packet p;
    p << CHAT;
    p << id;
    p << (uint16_t) message.size();
    p << message;

    send(p);
}

void session::read_command() {
    auto self(shared_from_this());
    auto command = make_shared<uint8_t>();
    async_read(socket, buffer(command.get(), sizeof(*command)), [=](auto& error, auto) {
        if (error) return handle_error(error);
        switch (*command) {
            case WELCOME: {
                auto protocol_version = make_shared<uint16_t>();
                async_read(socket, buffer(protocol_version.get(), sizeof(*protocol_version)), [=](auto& error, auto) {
                    if (error) return handle_error(error);
                    self->read_command();
                });
                break;
            }

            case PONG: {
                auto timestamp = make_shared<uint64_t>();
                async_read(socket, buffer(timestamp.get(), sizeof(*timestamp)), [=](auto& error, auto) {
                    if (error) return handle_error(error);
                    latency_history.push_back((uint32_t)(get_time() - *timestamp) / 2);
                    while (latency_history.size() > 4) latency_history.pop_front();
                    self->read_command();
                });
                break;
            }

            case CONTROLLERS: {
                async_read(socket, buffer(controllers), [=](auto& error, auto) {
                    if (error) return handle_error(error);
                    self->read_command();
                });
                break;
            }

            case NAME: {
                auto name_length = make_shared<uint8_t>();
                async_read(socket, buffer(name_length.get(), sizeof *name_length), [=](auto& error, auto) {
                    if (error) return handle_error(error);
                    auto name = make_shared<wstring>(*name_length, L' ');
                    async_read(socket, buffer(*name), [=](auto& error, auto) {
                        if (error) return handle_error(error);
                        my_server.send_name(id, *name);
                        self->name = *name;
                        self->read_command();
                    });
                });
                break;
            }

            case CHAT: {
                auto message_length = make_shared<uint16_t>();
                async_read(socket, buffer(message_length.get(), sizeof *message_length), [=](auto& error, auto) {
                    if (error) return handle_error(error);
                    auto message = make_shared<wstring>(*message_length, L' ');
                    async_read(socket, buffer(*message), [=](auto& error, auto) {
                        if (error) return handle_error(error);
                        my_server.send_message(id, *message);
                        self->read_command();
                    });
                });
                break;
            }

            case LAG: {
                auto lag = make_shared<uint8_t>();
                async_read(socket, buffer(lag.get(), sizeof *lag), [=](auto& error, auto) {
                    if (error) return handle_error(error);
                    my_server.send_lag(id, *lag);
                    self->read_command();
                });
                break;
            }

            case AUTO_LAG: {
                my_server.auto_lag = !my_server.auto_lag;
                if (my_server.auto_lag) {
                    my_server.send_message(-1, L"Automatic lag is ENABLED");
                } else {
                    my_server.send_message(-1, L"Automatic lag is DISABLED");
                }
                self->read_command();
                break;
            }

            case START_GAME: {
                my_server.send_start_game();
                self->read_command();
                break;
            }

            case INPUT_DATA: {
                auto frame = make_shared<uint32_t>();
                async_read(socket, buffer(frame.get(), sizeof *frame), [=](auto& error, auto) {
                    if (error) return handle_error(error);
                    async_read(socket, buffer(in_buttons), [=](auto& error, auto) {
                        if (error) return handle_error(error);
                        my_server.send_input(id, player_index, *frame, in_buttons);
                        if (frame_history.empty() || *frame > get<0>(frame_history.back())) {
                            auto time = get_time();
                            frame_history.push_back(make_tuple(*frame, time));
                            while (get<1>(frame_history.front()) <= time - 1000) {
                                frame_history.pop_front();
                            }
                        }
                        self->read_command();
                    });
                });
                break;
            }

            default:
                self->read_command();
        }
    });
}

void session::send_lag(uint8_t lag) {
    send(packet() << LAG << lag);
}

void session::send_protocol_version() {
    send(packet() << WELCOME << MY_PROTOCOL_VERSION);
}

void session::send(const packet& p) {
    output_queue.push_back(p);
    flush();
}

void session::flush() {
    if (output_buffer.empty() && !output_queue.empty()) {
        do {
            output_buffer << output_queue.front();
            output_queue.pop_front();
        } while (!output_queue.empty());

        auto self(shared_from_this());
        async_write(socket, buffer(output_buffer.data()), [=](auto& error, auto) {
            output_buffer.clear();
            if (error) return handle_error(error);
            self->flush();
        });
    }
}

void session::send_input(uint8_t player_index, const vector<BUTTONS>& input) {
    for (size_t i = 0; i < input.size(); i++) {
        output_buttons[player_index + i].push_back(input[i]);
    }

    if (ready_to_send_input()) {
        send_input();
    }
}

bool session::is_me(uint32_t player) {
    return player_index <= player && player < player_index + in_buttons.size();
}

bool session::ready_to_send_input() {
    for (size_t i = 0; i < output_buttons.size(); i++) {
        if (!is_me(i) && output_buttons[i].empty()) {
            return false;
        }
    }

    return true;
}

void session::send_input() {
    packet p;
    p << INPUT_DATA;
    for (size_t i = 0; i < output_buttons.size(); i++) {
        if (!is_me(i)) {
            p << output_buttons[i].front();
            output_buttons[i].pop_front();
        }
    }

    send(p);
}

uint32_t session::get_fps() {
    return frame_history.size();
}