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

    my_dialog->set_command_handler([=](string command) {
        io_s.post([=] { process_command(command); });
    });

    game_started = false;
    lag = DEFAULT_LAG;
    current_lag.fill(0);
    frame = 0;
    golf = false;

    my_dialog->status("List of available commands:\n"
                      "* /name <name>              set your name\n"
                      "* /server <port>            host a server\n"
                      "* /connect <host> <port>    connect to a server\n"
                      "* /start                    start the game\n"
                      "* /lag <lag>                set the netplay input lag\n"
                      "* /autolag                  toggle automatic lag mode on and off\n"
                      "* /golf                     toggle golf mode on and off");
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

bool client::plugged_in(uint8_t index) {
    promise<bool> promise;
    io_s.post([&] { promise.set_value(local_controllers[index].Present); });
    return promise.get_future().get();
}

int client::netplay_to_local(int controller) {
    return my_controller_map.to_local(controller);
}

void client::set_local_controllers(CONTROL controllers[MAX_PLAYERS]) {
    for (int i = 0; i < MAX_PLAYERS; i++) {
        controllers[i].RawData = FALSE; // Disallow raw data
    }

    promise<void> promise;
    io_s.post([&] {
        if (game_started) return;
        for (size_t i = 0; i < MAX_PLAYERS; i++) {
            local_controllers[i] = controllers[i];
        }
        send_controllers();
        promise.set_value();
    });
    promise.get_future().get();
}

void client::process_input(int controller, BUTTONS* input) {
    promise<void> promise;
    io_s.post([&] {
        if (golf && lag != 0 && input->Z_TRIG) {
            send_lag(lag);
            set_lag(0);
        }

        current_lag[controller]--;
        while (current_lag[controller] < lag) {
            queue[controller].push(*input);
            send_input(controller, input);
            current_lag[controller]++;
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

void client::get_input(int controller, BUTTONS* input) {
    if (netplay_controllers[controller].Present) {
        try {
            *input = queue[controller].pop();
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

void client::process_command(string command) {
    if (command.substr(0, 1) == "/") {
        boost::char_separator<char> sep(" \t\n\r");
        boost::tokenizer<boost::char_separator<char>, string::const_iterator, string> tokens(command, sep);
        auto it = tokens.begin();

        if (*it == "/name") {
            if (++it != tokens.end()) {
                set_name(*it);
                send_name(*it);
            } else {
                my_dialog->error("Missing parameter.");
            }
        } else if (*it == "/server") {
            if (game_started) {
                my_dialog->error("Game has already started.");
                return;
            }

            if (++it == tokens.end()) {
                my_dialog->error("Missing parameter.");
                return;
            }

            try {
                uint16_t port = boost::lexical_cast<uint16_t>(*it);

                stop();
                if (my_server) {
                    my_server->stop();
                }
                my_server = shared_ptr<server>(new server(io_s, lag));

                port = my_server->start(port);

                my_dialog->status("Server is listening on port " + boost::lexical_cast<string>(port) + "...");

                if (port) {
                    connect("127.0.0.1", port);
                }
            } catch(const exception& e) {
                my_dialog->error(e.what());
            }
        } else if (*it == "/connect") {
            if (game_started) {
                my_dialog->error("Game has already started.");
                return;
            }

            if (++it == tokens.end()) {
                my_dialog->error("Missing parameter.");
                return;
            }

            string host = *it;

            if (++it == tokens.end()) {
                my_dialog->error("Missing parameter.");
                return;
            }

            try {
                uint16_t port = boost::lexical_cast<uint16_t>(*it);

                stop();
                if (my_server) {
                    my_server->stop();
                }

                connect(host, port);
            } catch (const exception& e) {
                my_dialog->error(e.what());
            }
        } else if (*it == "/start") {
            if (game_started) {
                my_dialog->error("Game has already started.");
                return;
            }
            send_start_game();
        } else if (*it == "/lag") {
            if (++it != tokens.end()) {
                try {
                    uint8_t lag = boost::numeric_cast<uint8_t>(boost::lexical_cast<uint32_t>(*it));

                    set_lag(lag);
                    send_lag(lag);
                } catch(const exception& e) {
                    my_dialog->error(e.what());
                }
            } else {
                my_dialog->error("Missing parameter.");
            }
        } else if (*it == "/autolag") {
            send_auto_lag();
        } else if (*it == "/my_lag") {
            if (++it != tokens.end()) {
                try {
                    uint8_t lag = boost::numeric_cast<uint8_t>(boost::lexical_cast<uint32_t>(*it));
                    set_lag(lag);
                } catch (const exception& e) {
                    my_dialog->error(e.what());
                }
            } else {
                my_dialog->error("Missing parameter.");
            }
        } else if (*it == "/your_lag") {
            if (++it != tokens.end()) {
                try {
                    uint8_t lag = boost::numeric_cast<uint8_t>(boost::lexical_cast<uint32_t>(*it));
                    send_lag(lag);
                } catch (const exception& e) {
                    my_dialog->error(e.what());
                }
            } else {
                my_dialog->error("Missing parameter.");
            }
        } else if (*it == "/golf") {
            golf = !golf;
            
            if (golf) {
                my_dialog->status("Golf mode is turned ON.");
            } else {
                my_dialog->status("Golf mode is turned OFF.");
            }
        } else {
            my_dialog->error("Unknown command: " + *it);
        }
    } else {
        my_dialog->chat(name, command);
        send_chat(command);
    }
}

void client::set_lag(uint8_t lag, bool show_message) {
    this->lag = lag;

    if (show_message) {
        my_dialog->status("Lag set to " + boost::lexical_cast<string>((int)lag) + ".");
    }
}

void client::game_has_started() {
    unique_lock<mutex> lock(mut);

    if (game_started) return;

    game_started = true;
    game_started_condition.notify_all();

    my_dialog->status("Game has started!");
}

void client::set_user_name(uint32_t id, const string& name) {
    if (names.find(id) == names.end()) {
        my_dialog->status(name + " has joined.");
    } else {
        my_dialog->status(names[id] + " is now " + name + ".");
    }

    names[id] = name;

    my_dialog->update_user_list(names, latencies);
}

void client::set_user_latency(uint32_t id, uint32_t latency) {
    latencies[id] = latency;
}

void client::remove_user(uint32_t id) {
    my_dialog->status(names[id] + " has left.");
    names.erase(id);
    latencies.erase(id);

    my_dialog->update_user_list(names, latencies);
}

void client::chat_received(int32_t id, const string& message) {
    if (id == -1) {
        my_dialog->status(message);
    } else if (id == -2) {
        my_dialog->error(message);
    } else {
        my_dialog->chat(names[id], message);
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

const map<uint32_t, string>& client::get_names() const {
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
        
        names.clear();
        latencies.clear();
        my_dialog->update_user_list(names, latencies);

        for (int i = 0; i < queue.size(); i++) {
            if (my_controller_map.to_local(i) == -1) {
                queue[i].interrupt(message_exception(error.message()));
            }
        }
    }

    my_dialog->error(error.message());
}

void client::connect(const string& host, uint16_t port) {
    my_dialog->status("Resolving...");
    resolver.async_resolve(ip::tcp::resolver::query(host, boost::lexical_cast<string>(port)), [=](const boost::system::error_code& error, ip::tcp::resolver::iterator iterator) {
        if (error) return handle_error(error, false);
        my_dialog->status("Resolved! Connecting to server...");
        ip::tcp::endpoint endpoint = *iterator;
        socket.async_connect(endpoint, [=](const boost::system::error_code& error) {
            if (error) return handle_error(error, false);

            boost::system::error_code ec;
            socket.set_option(ip::tcp::no_delay(true), ec);
            if (ec) return handle_error(ec, false);

            connected = true;

            my_dialog->status("Connected!");

            send_protocol_version();
            send_name(name);
            send_controllers();

            process_packet();
        });
    });
}

bool client::is_connected() {
    return connected;
}

void client::process_packet() {
    auto packet_size_packet = make_shared<packet>(sizeof uint32_t);
    async_read(socket, buffer(packet_size_packet->data()), [=](const boost::system::error_code& error, size_t transferred) {
        if (error) return handle_error(error, true);
        auto packet_size = packet_size_packet->read<uint32_t>();
        if (packet_size == 0) return process_packet();

        auto p = make_shared<packet>(packet_size);
        async_read(socket, buffer(p->data()), [=](const boost::system::error_code& error, size_t transferred) {
            if (error) return handle_error(error, true);

            auto command = p->read<uint8_t>();
            switch (command) {
                case VERSION: {
                    auto protocol_version = p->read<uint32_t>();
                    if (protocol_version != PROTOCOL_VERSION) {
                        stop();
                        my_dialog->error("Server protocol version does not match client protocol version.");
                    }
                    break;
                }

                case PING: {
                    auto timestamp = p->read<uint64_t>();
                    send(packet() << PONG << timestamp);
                    flush();
                    break;
                }

                case LATENCIES: {
                    auto user_count = p->read<uint32_t>();
                    if (user_count == 0) {
                        break;
                    }
                    vector<int32_t> data(user_count * 2);
                    p->read(data);
                    for (int i = 0; i < data.size(); i += 2) {
                        if (data[i + 1] >= 0) {
                            set_user_latency(data[i], data[i + 1]);
                        }
                    }
                    my_dialog->update_user_list(get_names(), get_latencies());
                    break;
                }

                case NAME: {
                    auto user_id = p->read<uint32_t>();
                    auto name_length = p->read<uint8_t>();
                    string name(name_length, ' ');
                    p->read(name);
                    set_user_name(user_id, name);
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
                    for (size_t i = 0; i < MAX_PLAYERS; i++) {
                        *p >> netplay_controllers[i].Plugin;
                        *p >> netplay_controllers[i].Present;
                        *p >> netplay_controllers[i].RawData;
                    }
                    for (size_t i = 0; i < MAX_PLAYERS; i++) {
                        my_controller_map.local_to_netplay[i] = p->read<int8_t>();
                    }
                    break;
                }

                case START: {
                    game_has_started();
                    break;
                }

                case INPUT_DATA: {
                    auto controller = p->read<uint8_t>();
                    BUTTONS buttons;
                    buttons.Value = p->read<uint32_t>();
                    try {
                        queue[controller].push(buttons);
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

void client::send_protocol_version() {
    if (!is_connected()) return;

    send(packet() << VERSION << PROTOCOL_VERSION);
    flush();
}

void client::send_name(const string& name) {
    if (!is_connected()) return;

    packet p;
    p << NAME;
    p << (uint8_t)name.size();
    p << name;

    send(p);
    flush();
}

void client::send_chat(const string& message) {
    if (!is_connected()) return;

    send(packet() << MESSAGE << (uint16_t)message.size() << message);
    flush();
}

void client::send_controllers() {
    if (!is_connected()) return;

    packet p;
    p << CONTROLLERS;
    for (auto& c : local_controllers) {
        p << c.Plugin << c.Present << c.RawData;
    }
    send(p);
    flush();
}

void client::send_start_game() {
    if (!is_connected()) return my_dialog->error("Cannot start game unless connected to server.");

    send(packet() << START);
    flush();
}

void client::send_lag(uint8_t lag) {
    if (!is_connected()) return;

    send(packet() << LAG << lag);
    flush();
}

void client::send_auto_lag() {
    if (!is_connected()) return my_dialog->error("Cannot toggle automatic lag unless connected to server.");

    send(packet() << AUTOLAG);
    flush();
}

void client::send_input(uint8_t controller, BUTTONS* input) {
    if (!is_connected()) return;

    send(packet() << INPUT_DATA << controller << input->Value);
}

void client::send_frame() {
    if (!is_connected()) return;

    send(packet() << FRAME << frame);
    flush();
}

void client::send(const packet& p) {
    output_queue.push_back(p);
}

void client::flush() {
    if (!output_buffer.empty() || output_queue.empty()) return; 

    do {
        output_buffer << output_queue.front().size() << output_queue.front();
        output_queue.pop_front();
    } while (!output_queue.empty());

    async_write(socket, buffer(output_buffer.data()), [=](const boost::system::error_code& error, size_t transferred) {
        output_buffer.clear();
        if (error) return handle_error(error, true);
        flush();
    });
}
