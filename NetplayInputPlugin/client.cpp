#include <functional>
#include <string>
#include <future>
#include <boost/lexical_cast.hpp>
#include <boost/numeric/conversion/cast.hpp>
#include <boost/tokenizer.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>

#include "client.h"
#include "client_dialog.h"
#include "util.h"

using namespace std;
using namespace boost::asio;

client::client(client_dialog* my_dialog)
    : my_dialog(my_dialog), work(io_s), resolver(io_s), socket(io_s), connected(false), thread([&] { io_s.run(); }) {

    my_dialog->set_command_handler([=](wstring command) {
        io_s.post([=] { process_command(command); });
    });

    game_started = false;
    online = false;
    current_lag = 0;
    lag = DEFAULT_LAG;
    frame = 0;
    golf = false;

    my_dialog->status(L"List of available commands:\n"
                      L"* /name <name>           -- set your name\n"
                      L"* /server <port>         -- host a server\n"
                      L"* /connect <host> <port> -- connect to a server\n"
                      L"* /start                 -- start the game\n"
                      L"* /lag <lag>             -- set the netplay input lag\n"
                      L"* /autolag               -- toggle automatic lag mode on and off\n"
                      L"* /golf                  -- toggle golf mode on and off");

    my_server = shared_ptr<server>(new server(lag));
}

client::~client() {
    io_s.post([&] { stop(); });
    io_s.stop();
    thread.join();

    my_server.reset();
}

wstring client::get_name() {
    promise<wstring> promise;
    io_s.post([&] { promise.set_value(name); });
    return promise.get_future().get();
}

void client::set_name(const wstring& name) {
    promise<void> promise;
    io_s.post([&] {
        this->name = name;
        my_dialog->status(L"Name set to " + name + L".");
        promise.set_value();
    });
    promise.get_future().get();
}

bool client::plugged_in(uint8_t index) {
    promise<bool> promise;
    io_s.post([&] { promise.set_value(local_controllers[index].Present); });
    return promise.get_future().get();
}

void client::set_local_controllers(CONTROL controllers[MAX_PLAYERS]) {
    int count = 0;
    for (int i = 0; i < MAX_PLAYERS; i++) {
        controllers[i].RawData = FALSE; // Disallow raw data
        if (controllers[i].Present) {
            count++;
        }
    }

    promise<void> promise;
    io_s.post([&] {
        if (game_started) return;
        local_controllers.assign(controllers, controllers + MAX_PLAYERS);
        send_controllers(local_controllers);
        my_dialog->status(L"Requested local players: " + boost::lexical_cast<wstring>(count));
        promise.set_value();
    });
    promise.get_future().get();
}

void client::process_input(vector<BUTTONS>& input) {
    promise<void> promise;
    io_s.post([&] {
        if (player_count > 0) {
            if (golf) {
                for (int i = 0; lag != 0 && i < input.size(); i++) {
                    if (local_controllers[i].Present && input[i].Z_TRIG) {
                        send_lag(lag);
                        set_lag(0);
                    }
                }
            }

            vector<BUTTONS> input_prime;
            for (int i = 0; i < input.size() && input_prime.size() < player_count; i++) {
                if (local_controllers[i].Present) {
                    input_prime.push_back(input[i]);
                }
            }

            current_lag--;

            while (current_lag < lag) {
                send_input(frame, input_prime);
                local_input.push_back(input_prime);
                current_lag++;
            }
        }

        enqueue_if_ready();

        frame++;

        promise.set_value();
    });
    promise.get_future().get();

    vector<BUTTONS> processed = queue.pop();

    for (int i = 0; i < processed.size(); i++) {
        input[i] = processed[i];
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

uint8_t client::get_remote_count() {
    return get_total_count() - player_count;
}

vector<CONTROL> client::get_local_controllers() {
    return local_controllers;
}

void client::process_command(wstring command) {
    if (command.substr(0, 1) == L"/") {
        boost::char_separator<wchar_t> sep(L" \t\n\r");
        boost::tokenizer<boost::char_separator<wchar_t>, wstring::const_iterator, wstring> tokens(command, sep);
        auto it = tokens.begin();

        if (*it == L"/name") {
            if (++it != tokens.end()) {
                set_name(*it);
                send_name(*it);
            } else {
                my_dialog->error(L"Missing parameter.");
            }
        } else if (*it == L"/server") {
            if (game_started) {
                my_dialog->error(L"Game has already started.");
                return;
            }

            if (++it == tokens.end()) {
                my_dialog->error(L"Missing parameter.");
                return;
            }

            try {
                uint16_t port = boost::lexical_cast<uint16_t>(*it);

                stop();
                my_server = shared_ptr<server>(new server(lag));

                port = my_server->start(port);

                my_dialog->status(L"Server is listening on port " + boost::lexical_cast<wstring>(port) + L"...");

                if (port) {
                    connect(L"127.0.0.1", port);
                }
            } catch(const exception& e) {
                my_dialog->error(L"\"" + widen(e.what()) + L"\"");
            }
        } else if (*it == L"/connect") {
            if (game_started) {
                my_dialog->error(L"Game has already started.");
                return;
            }

            if (++it == tokens.end()) {
                my_dialog->error(L"Missing parameter.");
                return;
            }

            wstring host = *it;

            if (++it == tokens.end()) {
                my_dialog->error(L"Missing parameter.");
                return;
            }

            try {
                uint16_t port = boost::lexical_cast<uint16_t>(*it);

                stop();
                my_server = shared_ptr<server>(new server(lag));

                connect(host, port);
            } catch (const exception& e) {
                my_dialog->error(L"\"" + widen(e.what()) + L"\"");
            }
        } else if (*it == L"/start") {
            if (game_started) {
                my_dialog->error(L"Game has already started.");
                return;
            }
            send_start_game();
        } else if (*it == L"/lag") {
            if (++it != tokens.end()) {
                try {
                    uint8_t lag = boost::numeric_cast<uint8_t>(boost::lexical_cast<uint32_t>(*it));

                    set_lag(lag);
                    send_lag(lag);
                } catch(const exception& e) {
                    my_dialog->error(L"\"" + widen(e.what()) + L"\"");
                }
            } else {
                my_dialog->error(L"Missing parameter.");
            }
        } else if (*it == L"/autolag") {
            send_auto_lag();
        } else if (*it == L"/my_lag") {
            if (++it != tokens.end()) {
                try {
                    uint8_t lag = boost::numeric_cast<uint8_t>(boost::lexical_cast<uint32_t>(*it));
                    set_lag(lag);
                } catch (const exception& e) {
                    my_dialog->error(L"\"" + widen(e.what()) + L"\"");
                }
            } else {
                my_dialog->error(L"Missing parameter.");
            }
        } else if (*it == L"/your_lag") {
            if (++it != tokens.end()) {
                try {
                    uint8_t lag = boost::numeric_cast<uint8_t>(boost::lexical_cast<uint32_t>(*it));
                    send_lag(lag);
                } catch (const exception& e) {
                    my_dialog->error(L"\"" + widen(e.what()) + L"\"");
                }
            } else {
                my_dialog->error(L"Missing parameter.");
            }
        } else if (*it == L"/golf") {
            golf = !golf;
            
            if (golf) {
                my_dialog->status(L"Golf mode is turned ON.");
            } else {
                my_dialog->status(L"Golf mode is turned OFF.");
            }
        } else {
            my_dialog->error(L"Unknown command: " + *it);
        }
    } else {
        my_dialog->chat(name, command);
        send_chat(command);
    }
}

void client::set_lag(uint8_t lag, bool show_message) {
    this->lag = lag;

    if (show_message) {
        my_dialog->status(L"Lag set to " + boost::lexical_cast<wstring>((int)lag) + L".");
    }
}

void client::game_has_started() {
    unique_lock<mutex> lock(mut);

    if (game_started) return;

    game_started = true;
    online = true;
    game_started_condition.notify_all();

    my_dialog->status(L"Game has started!");
}

void client::client_error() {
    online = false;
    enqueue_if_ready();

    names.clear();
    latencies.clear();
    my_dialog->update_user_list(names, latencies);
}

void client::update_netplay_controllers(const array<CONTROL, MAX_PLAYERS>& netplay_controllers) {
    for (int i = 0; i < netplay_controllers.size(); i++) {
        this->netplay_controllers[i] = netplay_controllers[i];
    }
}

void client::set_player_index(uint8_t player_index) {
    this->player_index = player_index;
}

void client::set_player_count(uint8_t player_count) {
    this->player_count = player_count;

    my_dialog->status(L"Local players: " + boost::lexical_cast<wstring>((int)player_count));
}

void client::set_user_name(uint32_t id, const wstring& name) {
    if (names.find(id) == names.end()) {
        my_dialog->status(name + L" has joined.");
    } else {
        my_dialog->status(names[id] + L" is now " + name + L".");
    }

    names[id] = name;

    my_dialog->update_user_list(names, latencies);
}

void client::set_user_latency(uint32_t id, uint32_t latency) {
    latencies[id] = latency;
}

void client::remove_user(uint32_t id) {
    my_dialog->status(names[id] + L" has left.");
    names.erase(id);
    latencies.erase(id);

    my_dialog->update_user_list(names, latencies);
}

void client::chat_received(int32_t id, const wstring& message) {
    if (id == -1) {
        my_dialog->status(message);
    } else if (id == -2) {
        my_dialog->error(message);
    } else {
        my_dialog->chat(names[id], message);
    }
}

void client::incoming_remote_input(const vector<BUTTONS>& input) {
    remote_input.push_back(input);

    enqueue_if_ready();
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

void client::enqueue_if_ready() {
    if (player_count > 0 && local_input.empty()) {
        return;
    }

    if (online && get_remote_count() > 0 && remote_input.empty()) {
        return;
    }

    vector<BUTTONS> input(get_total_count());
    for (int i = 0; i < input.size(); i++) {
        input[i].Value = 0;
    }

    if (!remote_input.empty()) {
        for (int i = 0; i < remote_input.front().size(); i++) {
            if (i < player_index) {
                input[i] = remote_input.front()[i];
            } else {
                input[i + player_count] = remote_input.front()[i];
            }
        }

        remote_input.pop_front();
    }

    if (!local_input.empty()) {
        for (int i = 0; i < local_input.front().size(); i++) {
            input[i + player_index] = local_input.front()[i];
        }

        local_input.pop_front();
    }

    queue.push(input);
}

const map<uint32_t, wstring>& client::get_names() const {
    return names;
}

const map<uint32_t, uint32_t>& client::get_latencies() const {
    return latencies;
}

void client::stop() {
    resolver.cancel();

    boost::system::error_code error;
    socket.shutdown(ip::tcp::socket::shutdown_both, error);
    socket.close(error);

    output_queue.clear();
    output_buffer.clear();

    connected = false;
}

void client::handle_error(const boost::system::error_code& error, bool lost_connection) {
    if (error == error::operation_aborted) return;

    if (lost_connection) {
        stop();
        client_error();
    }

    my_dialog->error(widen(error.message()));
}

void client::connect(const wstring& host, uint16_t port) {
    my_dialog->status(L"Resolving...");
    resolver.async_resolve(ip::tcp::resolver::query(narrow(host), boost::lexical_cast<string>(port)), [=](auto& error, auto iterator) {
        if (error) return handle_error(error, false);
        my_dialog->status(L"Resolved! Connecting to server...");
        ip::tcp::endpoint endpoint = *iterator;
        socket.async_connect(endpoint, [=](auto& error) {
            if (error) return handle_error(error, false);

            boost::system::error_code ec;
            socket.set_option(ip::tcp::no_delay(true), ec);
            if (ec) return handle_error(ec, false);

            connected = true;

            my_dialog->status(L"Connected!");

            send_protocol_version();
            send_name(name);
            send_controllers(get_local_controllers());

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
                        my_dialog->error(L"Server protocol version does not match client protocol version.");
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
                                set_user_latency((*data)[i], (*data)[i + 1]);
                            }
                        }
                        my_dialog->update_user_list(get_names(), get_latencies());
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
                            set_user_name(*user_id, *name);
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
                    remove_user(*user_id);
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
                            chat_received(*user_id, *message);
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
                        set_player_index(*player_index);
                        set_player_count(*player_count);
                        read_command();
                    });
                });
                break;
            }

            case CONTROLLERS: {
                auto incoming_controls = make_shared<array<CONTROL, MAX_PLAYERS>>();
                async_read(socket, buffer(*incoming_controls), [=](auto& error, auto) {
                    if (error) return handle_error(error, true);
                    update_netplay_controllers(*incoming_controls);
                    read_command();
                });
                break;
            }

            case START_GAME: {
                game_has_started();
                read_command();
                break;
            }

            case INPUT_DATA: {
                auto incoming_input = make_shared<vector<BUTTONS>>(get_remote_count());
                async_read(socket, buffer(*incoming_input), [=](auto& error, auto) {
                    if (error) return handle_error(error, true);
                    incoming_remote_input(*incoming_input);
                    read_command();
                });
                break;
            }

            case LAG: {
                auto lag = make_shared<uint8_t>();
                async_read(socket, buffer(lag.get(), sizeof *lag), [=](auto& error, auto) {
                    if (error) return handle_error(error, true);
                    set_lag(*lag, false);
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
    p << (uint8_t)name.size();
    p << name;

    send(p);
}

void client::send_chat(const wstring& message) {
    if (!is_connected()) return;

    packet p;
    p << CHAT;
    p << (uint16_t)message.size();
    p << message;

    send(p);
}

void client::send_controllers(const vector<CONTROL>& controllers) {
    if (!is_connected()) return;

    send(packet() << CONTROLLERS << controllers);
}

void client::send_start_game() {
    if (!is_connected()) return my_dialog->error(L"Cannot start game unless connected to server.");

    send(packet() << START_GAME);
}

void client::send_lag(uint8_t lag) {
    if (!is_connected()) return;

    send(packet() << LAG << lag);
}

void client::send_auto_lag() {
    if (!is_connected()) return my_dialog->error(L"Cannot toggle automatic lag unless connected to server.");

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
