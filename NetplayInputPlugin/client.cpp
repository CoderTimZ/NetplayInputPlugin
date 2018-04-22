#include <boost/lexical_cast.hpp>
#include <string>

#include "client.h"
#include "game.h"
#include "client_dialog.h"
#include "util.h"

using namespace std;
using namespace boost::asio;

client::client(client_dialog& my_dialog, game& my_game)
  : my_dialog(my_dialog), my_game(my_game), work(io_s), resolver(io_s), socket(io_s), thread(boost::bind(&io_service::run, &io_s)) {
    connected = false;
}

client::~client() {
    io_s.post(boost::bind(&client::stop, this));

    thread.join();
}

void client::stop() {
    resolver.cancel();

    boost::system::error_code error;
    socket.shutdown(boost::asio::ip::tcp::socket::shutdown_both, error);
    socket.close(error);

    output_queue.clear();
    output_buffer.clear();

    connected = false;

    io_s.stop();
}

void client::handle_error(const boost::system::error_code& error, bool lost_connection) {
    if (error == error::operation_aborted) return;

    if (lost_connection) {
        stop();
        my_game.client_error();
    }

    my_dialog.error(widen(error.message()));
}

void client::connect(const wstring& host, uint16_t port) {
    my_dialog.status(L"Resolving...");
    resolver.async_resolve(ip::tcp::resolver::query(narrow(host), boost::lexical_cast<string>(port)), [=](auto& error, auto iterator) {
        if (error) return handle_error(error, false);
        my_dialog.status(L"Resolved! Connecting to server...");
        ip::tcp::endpoint endpoint = *iterator;
        socket.async_connect(endpoint, [=](auto& error) {
            if (error) return handle_error(error, false);

            boost::system::error_code ec;
            socket.set_option(ip::tcp::no_delay(true), ec);
            if (ec) return handle_error(ec, false);

            connected = true;

            my_dialog.status(L"Connected!");

            send_protocol_version();
            send_name(my_game.get_name());
            send_controllers(my_game.get_local_controllers());

            read_command();
        });
    });
}

bool client::is_connected() {
    return connected;
}

void client::read_command() {
    auto command = make_shared<uint8_t>();
    async_read(socket, buffer(command.get(), sizeof *command), [=](auto& error, auto) {
        if (error) return handle_error(error, true);
        switch (*command) {
            case WELCOME: {
                auto protocol_version = make_shared<uint16_t>();
                async_read(socket, buffer(protocol_version.get(), sizeof *protocol_version), [=](auto& error, auto) {
                    if (error) return handle_error(error, true);
                    if (*protocol_version != MY_PROTOCOL_VERSION) {
                        stop();
                        my_dialog.error(L"Server protocol version does not match client protocol version.");
                    } else {
                        read_command();
                    }
                });
                break;
            }

            case PING: {
                auto timestamp = make_shared<uint64_t>();
                async_read(socket, buffer(timestamp.get(), sizeof *timestamp), [=](auto& error, auto) {
                    if (error) return handle_error(error, true);
                    send(packet() << PONG << *timestamp);
                    read_command();
                });
                break;
            }

            case LATENCIES: {
                auto user_count = make_shared<uint32_t>();
                async_read(socket, buffer(user_count.get(), sizeof *user_count), [=](auto& error, auto) {
                    if (error) return handle_error(error, true);
                    if (user_count == 0) return read_command();
                    auto data = make_shared<vector<int32_t>>(*user_count * 2);
                    async_read(socket, buffer(*data), [=](auto& error, auto) {
                        if (error) return handle_error(error, true);
                        for (int i = 0; i < data->size(); i += 2) {
                            if ((*data)[i + 1] >= 0) {
                                my_game.set_user_latency((*data)[i], (*data)[i + 1]);
                            }
                        }
                        my_dialog.update_user_list(my_game.get_names(), my_game.get_latencies());
                        read_command();
                    });
                });
                break;
            }

            case NAME: {
                auto user_id = make_shared<uint32_t>();
                async_read(socket, buffer(user_id.get(), sizeof *user_id), [=](auto& error, auto) {
                    if (error) return handle_error(error, true);
                    auto name_length = make_shared<uint8_t>();
                    async_read(socket, buffer(name_length.get(), sizeof *name_length), [=](auto& error, auto) {
                        if (error) return handle_error(error, true);
                        auto name = make_shared<wstring>(*name_length, L' ');
                        async_read(socket, buffer(*name), [=](auto& error, auto) {
                            if (error) return handle_error(error, true);
                            my_game.set_user_name(*user_id, *name);
                            read_command();
                        });
                    });
                });
                break;
            }

            case QUIT: {
                auto user_id = make_shared<uint32_t>();
                async_read(socket, buffer(user_id.get(), sizeof *user_id), [=](auto& error, auto) {
                    if (error) return handle_error(error, true);
                    my_game.remove_user(*user_id);
                    read_command();
                });
                break;
            }

            case CHAT: {
                auto user_id = make_shared<int32_t>();
                async_read(socket, buffer(user_id.get(), sizeof *user_id), [=](auto& error, auto) {
                    if (error) return handle_error(error, true);
                    auto message_length = make_shared<uint16_t>();
                    async_read(socket, buffer(message_length.get(), sizeof *message_length), [=](auto& error, auto) {
                        if (error) return handle_error(error, true);
                        auto message = make_shared<wstring>(*message_length, L' ');
                        async_read(socket, buffer(*message), [=](auto& error, auto) {
                            if (error) return handle_error(error, true);
                            my_game.chat_received(*user_id, *message);
                            read_command();
                        });
                    });
                });
                break;
            }

            case PLAYER_RANGE: {
                auto player_index = make_shared<uint8_t>();
                async_read(socket, buffer(player_index.get(), sizeof *player_index), [=](auto& error, auto) {
                    if (error) return handle_error(error, true);
                    auto player_count = make_shared<uint8_t>();
                    async_read(socket, buffer(player_count.get(), sizeof *player_count), [=](auto& error, auto) {
                        if (error) return handle_error(error, true);
                        my_game.set_player_index(*player_index);
                        my_game.set_player_count(*player_count);
                        read_command();
                    });
                });
                break;
            }

            case CONTROLLERS: {
                auto incoming_controls = make_shared<std::array<CONTROL, MAX_PLAYERS>>();
                async_read(socket, buffer(*incoming_controls), [=](auto& error, auto) {
                    if (error) return handle_error(error, true);
                    my_game.update_netplay_controllers(*incoming_controls);
                    read_command();
                });
                break;
            }

            case START_GAME: {
                my_game.game_has_started();
                read_command();
                break;
            }

            case INPUT_DATA: {
                auto incoming_input = make_shared<std::vector<BUTTONS>>(my_game.get_remote_count());
                async_read(socket, buffer(*incoming_input), [=](auto& error, auto) {
                    if (error) return handle_error(error, true);
                    my_game.incoming_remote_input(*incoming_input);
                    read_command();
                });
                break;
            }

            case LAG: {
                auto lag = make_shared<uint8_t>();
                async_read(socket, buffer(lag.get(), sizeof *lag), [=](auto& error, auto) {
                    if (error) return handle_error(error, true);
                    my_game.set_lag(*lag, false);
                    read_command();
                });
                break;
            }

            default:
                read_command();
        }
    });
}

void client::send_protocol_version() {
    if (!is_connected()) return;
    
    send(packet() << WELCOME << MY_PROTOCOL_VERSION);
}

void client::send_name(const wstring& name) {
    if (!is_connected()) return;

    packet p;
    p << NAME;
    p << (uint8_t) name.size();
    p << name;

    send(p);
}

void client::send_chat(const wstring& message) {
    if (!is_connected()) return;

    packet p;
    p << CHAT;
    p << (uint16_t) message.size();
    p << message;

    send(p);
}

void client::send_controllers(const vector<CONTROL>& controllers) {
    if (!is_connected()) return;

    send(packet() << CONTROLLERS << controllers);
}

void client::send_start_game() {
    if (!is_connected()) return my_dialog.error(L"Cannot start game unless connected to server.");

    send(packet() << START_GAME);
}

void client::send_lag(uint8_t lag) {
    if (!is_connected()) return;

    send(packet() << LAG << lag);
}

void client::send_auto_lag() {
    if (!is_connected()) return my_dialog.error(L"Cannot toggle automatic lag unless connected to server.");

    send(packet() << AUTO_LAG);
}

void client::send_input(uint32_t frame, const vector<BUTTONS>& input) {
    if (!is_connected()) return;

    send(packet() << INPUT_DATA << frame << input);
}

void client::send(const packet& p) {
    output_queue.push_back(p);
    flush();
}

void client::flush() {
    if (output_buffer.empty() && !output_queue.empty()) {
        do {
            output_buffer << output_queue.front();
            output_queue.pop_front();
        } while (!output_queue.empty());

        async_write(socket, buffer(output_buffer.data()), [=](auto& error, auto) {
            output_buffer.clear();
            if (error) return handle_error(error, true);
            flush();
        });
    }
}
