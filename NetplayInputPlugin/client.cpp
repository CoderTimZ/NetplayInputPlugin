#include <functional>
#include <string>
#include <future>
#include <memory>

#include "client.h"
#include "client_dialog.h"
#include "util.h"

using namespace std;
using namespace asio;

client::client(client_dialog* my_dialog)
    : my_dialog(my_dialog), work(io_s), resolver(io_s), socket(io_s), connected(false), thread([&] { io_s.run(); }) {

    my_dialog->set_message_handler([=](string message) {
        io_s.post([=] { process_message(message); });
    });

    my_dialog->set_destroy_handler([=] {
        game_started_condition.notify_all();
    });

    game_started = false;
    lag = DEFAULT_LAG;
    current_lag.fill(0);
    frame = 0;
    golf = false;

    my_dialog->status("List of available commands:\n"
                      "- /name <name>            set your name\n"
                      "- /host [port]            host a server\n"
                      "- /join <address> [port]  join a server\n"
                      "- /start                  start the game\n"
                      "- /lag <lag>              set the netplay input lag\n"
                      "- /autolag                toggle automatic lag mode on and off\n"
                      "- /golf                   toggle golf mode on and off");
}

client::~client() {
    if (my_server) {
        my_server->stop();
        my_server.reset();
    }

    io_s.post([&] { stop(); });

    io_s.stop();
    thread.join();
}

string client::get_name() {
    promise<string> promise;
    io_s.post([&] { promise.set_value(name); });
    return promise.get_future().get();
}

void client::set_name(const string& name) {
    promise<void> promise;
    io_s.post([&] {
        this->name = name;
        my_dialog->status("Name set to " + name + ".");
        promise.set_value();
    });
    promise.get_future().get();
}

int client::netplay_to_local(int port) {
    return my_controller_map.to_local(port);
}

void client::set_local_controllers(CONTROL controllers[MAX_PLAYERS]) {
    for (int i = 0; i < MAX_PLAYERS; i++) {
        controllers[i].RawData = FALSE; // Disallow raw data
    }

    promise<void> promise;
    io_s.post([&] {
        for (size_t i = 0; i < MAX_PLAYERS; i++) {
            local_controllers[i] = controllers[i];
        }
        send_controllers();
        promise.set_value();
    });
    promise.get_future().get();
}

void client::process_input(int port, BUTTONS* input) {
    promise<void> promise;
    io_s.post([&] {
        if (golf && lag != 0 && input->Z_TRIG) {
            send_lag(lag);
            set_lag(0);
        }

        current_lag[port]--;
        while (current_lag[port] < lag) {
            input_queues[port].push(*input);
            send_input(port, input);
            current_lag[port]++;
        }

        promise.set_value();
    });
    promise.get_future().get();
}

void client::frame_complete() {
    io_s.post([&] {
        send_frame();
        frame++;
    });
}

void client::get_input(int port, BUTTONS* input) {
    if (netplay_controllers[port].Present) {
        try {
            *input = input_queues[port].pop();
        } catch (const exception&) {
            input->Value = 0;
        }
    } else {
        input->Value = 0;
    }
}

void client::set_netplay_controllers(CONTROL netplay_controllers[MAX_PLAYERS]) {
    promise<void> promise;
    io_s.post([&] {
        this->netplay_controllers = netplay_controllers;
        promise.set_value();
    });
    promise.get_future().get();
}

void client::wait_for_game_to_start() {
    unique_lock<mutex> lock(mut);

    game_started_condition.wait(lock, [=] { return game_started; });
}

void client::process_message(string message) {
    if (message.substr(0, 1) == "/") {
        vector<string> params;
        for (int start = 0, end = 0; end != string::npos; start = end + 1) {
            end = message.find(" ", start);
            string param = message.substr(start, end == string::npos ? string::npos : end - start);
            if (!param.empty()) params.push_back(param);
        }

        if (params[0] == "/name") {
            if (params.size() >= 2) {
                name = params[1];
                my_dialog->status("Name set to " + name + ".");
                send_name();
            } else {
                my_dialog->error("Missing parameter.");
            }
        } else if (params[0] == "/host" || params[0] == "/server") {
            if (game_started) {
                my_dialog->error("Game has already started.");
                return;
            }

            try {
                uint16_t port = params.size() >= 2 ? stoi(params[1]) : 6400;

                stop();
                if (my_server) {
                    my_server->stop();
                }
                my_server = shared_ptr<server>(new server(io_s, lag));

                port = my_server->start(port);

                my_dialog->status("Server is listening on port " + to_string(port) + "...");

                if (port) {
                    connect("127.0.0.1", port);
                }
            } catch(const exception& e) {
                my_dialog->error(e.what());
            }
        } else if (params[0] == "/join" || params[0] == "/connect") {
            if (game_started) {
                my_dialog->error("Game has already started.");
                return;
            }

            if (params.size() < 2) {
                my_dialog->error("Missing parameter.");
                return;
            }

            string host = params[1];

            try {
                uint16_t port = params.size() >= 3 ? stoi(params[2]) : 6400;

                stop();
                if (my_server) {
                    my_server->stop();
                }

                connect(host, port);
            } catch (const exception& e) {
                my_dialog->error(e.what());
            }
        } else if (params[0] == "/start") {
            if (game_started) {
                my_dialog->error("Game has already started.");
                return;
            }
            send_start_game();
        } else if (params[0] == "/lag") {
            if (params.size() >= 2) {
                try {
                    uint8_t lag = stoi(params[1]);
                    set_lag(lag);
                    send_lag(lag);
                } catch(const exception& e) {
                    my_dialog->error(e.what());
                }
            } else {
                my_dialog->error("Missing parameter.");
            }
        } else if (params[0] == "/autolag") {
            send_autolag();
        } else if (params[0] == "/my_lag") {
            if (params.size() >= 2) {
                try {
                    uint8_t lag = stoi(params[1]);
                    set_lag(lag);
                } catch (const exception& e) {
                    my_dialog->error(e.what());
                }
            } else {
                my_dialog->error("Missing parameter.");
            }
        } else if (params[0] == "/your_lag") {
            if (params.size() >= 2) {
                try {
                    uint8_t lag = stoi(params[1]);
                    send_lag(lag);
                } catch (const exception& e) {
                    my_dialog->error(e.what());
                }
            } else {
                my_dialog->error("Missing parameter.");
            }
        } else if (params[0] == "/golf") {
            golf = !golf;
            
            if (golf) {
                my_dialog->status("Golf mode is turned ON.");
            } else {
                my_dialog->status("Golf mode is turned OFF.");
            }
        } else {
            my_dialog->error("Unknown command: " + params[0]);
        }
    } else {
        my_dialog->chat(name, message);
        send_chat(message);
    }
}

void client::set_lag(uint8_t lag, bool show_message) {
    this->lag = lag;

    if (show_message) {
        my_dialog->status("Lag set to " + to_string((int)lag) + ".");
    }
}

void client::game_has_started() {
    unique_lock<mutex> lock(mut);

    if (game_started) return;

    game_started = true;
    game_started_condition.notify_all();

    my_dialog->status("Game has started!");
}

void client::remove_user(uint32_t user_id) {
    my_dialog->status(users[user_id].name + " has quit.");
    users.erase(user_id);
    my_dialog->update_user_list(users);
}

void client::chat_received(int32_t user_id, const string& message) {
    switch (user_id) {
        case -2:
            my_dialog->error(message);
            break;

        case -1:
            my_dialog->status(message);
            break;

        default:
            my_dialog->chat(users[user_id].name, message);
    }
}

uint8_t client::get_total_count() {
    uint8_t count = 0;

    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (netplay_controllers[i].Present) {
            count++;
        }
    }

    return count;
}

void client::stop() {
    resolver.cancel();

    error_code error;
    socket.shutdown(ip::tcp::socket::shutdown_both, error);
    socket.close(error);

    output_buffer.clear();

    connected = false;
}

void client::handle_error(const error_code& error, bool lost_connection) {
    if (error == error::operation_aborted) return;

    if (lost_connection) {
        stop();
        
        users.clear();
        my_dialog->update_user_list(users);

        for (size_t i = 0; i < input_queues.size(); i++) {
            if (my_controller_map.to_local(i) == -1) {
                input_queues[i].interrupt(message_exception(error.message()));
            }
        }
    }

    my_dialog->error(error == error::eof ? "Disconnected from server" : error.message());
}

void client::connect(const string& host, uint16_t port) {
    my_dialog->status("Resolving...");
    resolver.async_resolve(ip::tcp::resolver::query(host, to_string(port)), [=](const error_code& error, ip::tcp::resolver::iterator iterator) {
        if (error) return handle_error(error, false);
        my_dialog->status("Resolved! Connecting to server...");
        ip::tcp::endpoint endpoint = *iterator;
        socket.async_connect(endpoint, [=](const error_code& error) {
            if (error) return handle_error(error, false);

            error_code ec;
            socket.set_option(ip::tcp::no_delay(true), ec);
            if (ec) return handle_error(ec, false);

            connected = true;

            my_dialog->status("Connected!");

            send_join();

            process_packet();
        });
    });
}

bool client::is_connected() {
    return connected;
}

void client::process_packet() {
    async_read(socket, buffer(packet_size_buffer, sizeof packet_size_buffer), [=](const error_code& error, size_t transferred) {
        if (error) return handle_error(error, true);
        uint16_t packet_size = (packet_size_buffer[0] << 8) | packet_size_buffer[1];
        if (packet_size == 0) return process_packet();

        auto p = make_shared<packet>(packet_size);
        async_read(socket, buffer(p->data()), [=](const error_code& error, size_t transferred) {
            if (error) return handle_error(error, true);

            auto packet_type = p->read<uint8_t>();
            switch (packet_type) {
                case VERSION: {
                    auto protocol_version = p->read<uint32_t>();
                    if (protocol_version != PROTOCOL_VERSION) {
                        stop();
                        my_dialog->error("Server protocol version does not match client protocol version.");
                    }
                    break;
                }

                case JOIN: {
                    auto user_id = p->read<uint32_t>();
                    auto name_length = p->read<uint8_t>();
                    string name(name_length, ' ');
                    p->read(name);
                    my_dialog->status(name + " joined.");
                    users[user_id].name = name;
                    my_dialog->update_user_list(users);
                    break;
                }

                case PING: {
                    auto timestamp = p->read<uint64_t>();
                    send(packet() << PONG << timestamp);
                    break;
                }

                case LATENCY: {
                    while (p->bytes_remaining() >= 8) {
                        auto user_id = p->read<uint32_t>();
                        auto latency = p->read<uint32_t>();
                        users[user_id].latency = latency;
                    }
                    my_dialog->update_user_list(users);
                    break;
                }

                case NAME: {
                    auto user_id = p->read<uint32_t>();
                    auto name_length = p->read<uint8_t>();
                    string name(name_length, ' ');
                    p->read(name);
                    my_dialog->status(users[user_id].name + " is now " + name + ".");
                    users[user_id].name = name;
                    my_dialog->update_user_list(users);
                    break;
                }

                case QUIT: {
                    auto user_id = p->read<uint32_t>();
                    remove_user(user_id);
                    break;
                }

                case MESSAGE: {
                    auto user_id = p->read<int32_t>();
                    auto message_length = p->read<uint16_t>();
                    string message(message_length, ' ');
                    p->read(message);
                    chat_received(user_id, message);
                    break;
                }

                case CONTROLLERS: {
                    auto user_id = p->read<int32_t>();
                    if (user_id == -1) {
                        for (size_t i = 0; i < MAX_PLAYERS; i++) {
                            *p >> netplay_controllers[i].Plugin;
                            *p >> netplay_controllers[i].Present;
                            *p >> netplay_controllers[i].RawData;
                        }
                        for (size_t i = 0; i < MAX_PLAYERS; i++) {
                            my_controller_map.local_to_netplay[i] = p->read<int8_t>();
                        }
                    } else {
                        for (size_t i = 0; i < MAX_PLAYERS; i++) {
                            *p >> users[user_id].controllers[i].Plugin;
                            *p >> users[user_id].controllers[i].Present;
                            *p >> users[user_id].controllers[i].RawData;
                        }
                        for (size_t i = 0; i < MAX_PLAYERS; i++) {
                            users[user_id].control_map.local_to_netplay[i] = p->read<int8_t>();
                        }
                        my_dialog->update_user_list(users);
                    }
                    break;
                }

                case START: {
                    game_has_started();
                    break;
                }

                case INPUT_DATA: {
                    auto port = p->read<uint8_t>();
                    BUTTONS buttons;
                    buttons.Value = p->read<uint32_t>();
                    try {
                        input_queues[port].push(buttons);
                    } catch (const exception&) { }
                    break;
                }

                case LAG: {
                    auto lag = p->read<uint8_t>();
                    set_lag(lag, false);
                    break;
                }
            }

            process_packet();
        });
    });
}

void client::send_join() {
    if (!is_connected()) return;

    packet p;

    p << JOIN << PROTOCOL_VERSION << (uint8_t)name.size() << name;

    for (auto& c : local_controllers) {
        p << c.Plugin << c.Present << c.RawData;
    }

    send(p);
}

void client::send_name() {
    if (!is_connected()) return;

    packet p;
    p << NAME;
    p << (uint8_t)name.size();
    p << name;

    send(p);
}

void client::send_chat(const string& message) {
    if (!is_connected()) return;

    send(packet() << MESSAGE << (uint16_t)message.size() << message);
}

void client::send_controllers() {
    if (!is_connected()) return;

    packet p;
    p << CONTROLLERS;
    for (auto& c : local_controllers) {
        p << c.Plugin << c.Present << c.RawData;
    }
    send(p);
}

void client::send_start_game() {
    if (!is_connected()) return my_dialog->error("Cannot start game unless connected to server.");

    send(packet() << START << 0);
}

void client::send_lag(uint8_t lag) {
    if (!is_connected()) return;

    send(packet() << LAG << lag);
}

void client::send_autolag() {
    if (!is_connected()) return my_dialog->error("Cannot toggle automatic lag unless connected to server.");

    send(packet() << AUTOLAG);
}

void client::send_input(uint8_t port, BUTTONS* input) {
    if (!is_connected()) return;

    send(packet() << INPUT_DATA << port << input->Value, false);
}

void client::send_frame() {
    if (!is_connected()) return;

    send(packet() << FRAME << frame);
}

void client::send(const packet& p, bool f) {
    assert(p.size() <= 0xFFFF);
    output_buffer.push_back(p.size() >> 8);
    output_buffer.push_back(p.size() & 0xFF);
    output_buffer.insert(output_buffer.end(), p.data().begin(), p.data().end());
    if (f) flush();
}

void client::flush() {
    if (writing || output_buffer.empty()) return;

    auto b = make_shared<vector<uint8_t>>(output_buffer);
    output_buffer.clear();
    writing = true;
    async_write(socket, buffer(*b), [this, b](const error_code& error, size_t transferred) {
        writing = false;
        if (error) return handle_error(error, true);
        flush();
    });
}
