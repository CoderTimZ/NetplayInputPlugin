#include <boost/bind.hpp>

#include "server.h"
#include "session.h"
#include "client_server_common.h"

using namespace std;
using namespace boost::asio;

session::session(server& my_server, uint32_t id)
    : socket(my_server.io_s), my_server(my_server), id(id), next_ping_id(0), pending_pong_id(-1), controllers(MAX_PLAYERS) { }

void session::handle_error(const boost::system::error_code& error) {
    if (error == error::operation_aborted) {
        return;
    }

    my_server.remove_session(id);
}

void session::stop() {
    boost::system::error_code error;
    socket.shutdown(boost::asio::ip::tcp::socket::shutdown_both, error);
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

const int32_t session::get_latency() const {
    return latency;
}

void session::send_controller_range(uint8_t player_index, uint8_t player_count) {
    this->player_index = player_index;
    in_buttons.resize(player_count);

    send(packet() << PLAYER_RANGE << player_index << player_count);
}

void session::send_controllers(const vector<CONTROL>& controllers) {
    assert(controllers.size() == MAX_PLAYERS);

    int total_count = 0;
    for (int i = 0; i < controllers.size(); i++) {
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
    send(packet() << PING << next_ping_id);

    QueryPerformanceCounter(&time_of_ping);
    pending_pong_id = next_ping_id;
    next_ping_id = (next_ping_id + 1) & 0x7FFFFFFF;
}

void session::cancel_ping() {
    pending_pong_id = -1;
    latency = -1;
}

void session::send_departure(uint32_t id) {
    send(packet() << QUIT << id);
}

void session::send_message(uint32_t id, const wstring& message) {
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
                auto pong_id = make_shared<int32_t>();
                async_read(socket, buffer(pong_id.get(), sizeof(*pong_id)), [=](auto& error, auto) {
                    if (error) return handle_error(error);
                    if (*pong_id == pending_pong_id) {
                        pending_pong_id = -1;
                        LARGE_INTEGER time_of_pong;
                        QueryPerformanceCounter(&time_of_pong);
                        latency = (time_of_pong.QuadPart - time_of_ping.QuadPart) / 2 * 1000 / my_server.performance_frequency.QuadPart;
                    }
                    my_server.send_latencies();
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

            case START_GAME: {
                my_server.send_start_game();
                self->read_command();
                break;
            }

            case INPUT_DATA: {
                async_read(socket, buffer(in_buttons), [=](auto& error, auto) {
                    if (error) return handle_error(error);
                    my_server.send_input(id, player_index, in_buttons);
                    self->read_command();
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
    for (int i = 0; i < input.size(); i++) {
        output_buttons[player_index + i].push_back(input[i]);
    }

    if (ready_to_send_input()) {
        send_input();
    }
}

bool session::is_me(uint8_t player) {
    return player_index <= player && player < player_index + in_buttons.size();
}

bool session::ready_to_send_input() {
    for (int i = 0; i < output_buttons.size(); i++) {
        if (!is_me(i) && output_buttons[i].empty()) {
            return false;
        }
    }

    return true;
}

void session::send_input() {
    packet p;
    p << INPUT_DATA;
    for (int i = 0; i < output_buttons.size(); i++) {
        if (!is_me(i)) {
            p << output_buttons[i].front();
            output_buttons[i].pop_front();
        }
    }

    send(p);
}

bool session::is_pong_pending() {
    return pending_pong_id != -1;
}
