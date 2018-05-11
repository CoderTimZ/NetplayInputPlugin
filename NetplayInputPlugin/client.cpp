#include "stdafx.h"

#import "msxml6.dll" 

#include "client.h"
#include "client_dialog.h"
#include "util.h"
#include "uri.h"

using namespace std;
using namespace asio;

client::client(shared_ptr<io_service> io_s, shared_ptr<client_dialog> my_dialog)
    : connection(io_s), my_dialog(my_dialog), work(*io_s), resolver(*io_s), thread([&] { io_s->run(); }) {

    my_dialog->set_message_handler([=](string message) {
        this->io_s->post([=] { process_message(message); });
    });

    my_dialog->set_close_handler([=] {
        this->io_s->post([=] {
            if (started) {
                this->my_dialog->minimize();
            } else {
                this->my_dialog->destroy();
                close();
                map_local_to_netplay();
                start_game();
            }
        });
    });

    frame = 0;
    golf = false;

    my_dialog->status("Available Commands:\n"
                      "/name <name> ........... Set your name\n"
                      "/join <address> ........ Join a game\n"
                      "/host [port] ........... Host a game\n"
                      "/start ................. Start the game\n"
                      "/lag <lag> ............. Set the netplay input lag\n"
                      "/autolag ............... Toggle automatic lag on and off\n"
                      "/golf .................. Toggle golf mode on and off");
}

client::~client() {
    if (thread.get_id() != this_thread::get_id()) {
        io_s->stop();
        thread.join();
    } else {
        thread.detach();
    }
}

void client::load_public_server_list() {
    auto self(shared_from_this());
    std::thread([=] {
        using namespace MSXML2;

        IXMLHTTPRequestPtr pIXMLHTTPRequest = NULL;
        if (pIXMLHTTPRequest.CreateInstance("Msxml2.XMLHTTP.6.0") < 0) return;
        if (pIXMLHTTPRequest->open("GET", "https://www.play64.com/server-list.txt", false) < 0) return;
        if (pIXMLHTTPRequest->send() < 0) return;

        string servers(pIXMLHTTPRequest->responseText);

        self->io_s->post([=] {
            self->public_servers.clear();
            for (size_t start = 0, end = 0; end != string::npos; start = end + 1) {
                end = servers.find("\n", start);
                string server = servers.substr(start, end == string::npos ? string::npos : end - start);
                if (!server.empty()) self->public_servers[server] = -1;
            }
            self->my_dialog->update_server_list(public_servers);
            self->ping_public_server_list();
        });
    }).detach();
}

void client::ping_public_server_list() {
    auto self(shared_from_this());

    for (auto& e : public_servers) {
        auto c = make_shared<connection>(io_s);
        auto host = e.first;

        auto set_ping = [self, c, host](double ping) {
            self->public_servers[host] = ping;
            self->my_dialog->update_server_list(self->public_servers);
            c->close();
        };

        resolver.async_resolve(ip::tcp::resolver::query(host, "6400"), [=](const error_code& error, ip::tcp::resolver::iterator iterator) {
            if (error) return set_ping(-2);
            ip::tcp::endpoint endpoint = *iterator;
            c->socket.async_connect(endpoint, [=](error_code error) {
                if (error) return set_ping(-2);

                c->socket.set_option(ip::tcp::no_delay(true), error);
                if (error) return set_ping(-2);

                c->read([=](packet& p) {
                    switch (p.read<uint8_t>()) {
                        case VERSION: {
                            auto protocol_version = p.read<uint32_t>();
                            if (protocol_version != PROTOCOL_VERSION) return set_ping(-3);
                            c->send(packet() << PING << timestamp());
                            c->read([=](packet& p) {
                                switch (p.read<uint8_t>()) {
                                    case PONG:
                                        set_ping(timestamp() - p.read<double>());
                                        break;

                                    default:
                                        set_ping(-3);
                                }
                            });
                            break;
                        }

                        default:
                            set_ping(-3);
                    }
                });
            });
        });
    }
}

string client::get_name() {
    promise<string> promise;
    io_s->post([&] { promise.set_value(name); });
    return promise.get_future().get();
}

void client::set_name(const string& name) {
    promise<void> promise;
    io_s->post([&] {
        this->name = name;
        my_dialog->status("Your name is " + name);
        promise.set_value();
    });
    promise.get_future().get();
}

void client::set_local_controllers(CONTROL controllers[MAX_PLAYERS]) {
    for (int i = 0; i < MAX_PLAYERS; i++) {
        controllers[i].RawData = FALSE; // Disallow raw data
    }

    promise<void> promise;
    io_s->post([&] {
        for (size_t i = 0; i < MAX_PLAYERS; i++) {
            local_controllers[i] = controllers[i];
        }
        send_controllers();
        promise.set_value();
    });
    promise.get_future().get();
}

void client::process_input(BUTTONS local_input[MAX_PLAYERS]) {
    promise<void> promise;
    io_s->post([&] {
        for (int netplay_port = 0; netplay_port < MAX_PLAYERS; netplay_port++) {
            int local_port = my_controller_map.to_local(netplay_port);
            if (local_port >= 0) {
                if (golf && lag != 0 && local_input[local_port].Z_TRIG) {
                    send_lag(lag);
                    set_lag(0);
                }
                while (input_queues[netplay_port].size() <= lag) {
                    input_queues[netplay_port].push(local_input[local_port]);
                    send_input(netplay_port, local_input[local_port]);
                }
            } else if (netplay_controllers[netplay_port].Present && !socket.is_open()) {
                while (input_queues[netplay_port].size() <= lag) {
                    input_queues[netplay_port].push(BUTTONS{ 0 });
                }
            }
        }

        send_frame();
        frame++;

        promise.set_value();
    });
    promise.get_future().get();
}

void client::get_input(int port, BUTTONS* input) {
    if (netplay_controllers[port].Present) {
        *input = input_queues[port].pop();
    } else {
        input->Value = 0;
    }
}

void client::set_netplay_controllers(CONTROL netplay_controllers[MAX_PLAYERS]) {
    promise<void> promise;
    io_s->post([&] {
        this->netplay_controllers = netplay_controllers;
        promise.set_value();
    });
    promise.get_future().get();
}

void client::post_close() {
    io_s->post([&] {
        close();
        map_local_to_netplay();
        start_game();
    });
}

void client::wait_until_start() {
    if (started) return;

    unique_lock<mutex> lock(mut);
    start_condition.wait(lock, [=] { return started; });
}

void client::process_message(string message) {
    try {
        if (message.substr(0, 1) == "/") {
            vector<string> params;
            for (size_t start = 0, end = 0; end != string::npos; start = end + 1) {
                end = message.find(" ", start);
                string param = message.substr(start, end == string::npos ? string::npos : end - start);
                if (!param.empty()) params.push_back(param);
            }

            if (params[0] == "/name") {
                if (params.size() < 2) throw runtime_error("Missing parameter");

                name = params[1];
                my_dialog->status("Your name is now " + name);
                send_name();
            } else if (params[0] == "/host" || params[0] == "/server") {
                if (started) throw runtime_error("Game has already started");

                port = params.size() >= 2 ? stoi(params[1]) : 6400;
                close();
                my_server = make_shared<server>(io_s, false);
                host = "127.0.0.1";
                port = my_server->open(port);
                path = "/";
                my_dialog->status("Server is listening on port " + to_string(port) + "...");
                connect(host, port, path);
            } else if (params[0] == "/join" || params[0] == "/connect") {
                if (started) throw runtime_error("Game has already started");
                if (params.size() < 2) throw runtime_error("Missing parameter");

                uri u(params[1]);
                if (!u.scheme.empty() && u.scheme != "play64") {
                    throw runtime_error("Unsupported protocol: " + u.scheme);
                }
                host = u.host;
                port = params.size() >= 3 ? stoi(params[2]) : (u.port == 0 ? 6400 : u.port);
                path = u.path;
                close();
                connect(host, port, path);
            } else if (params[0] == "/start") {
                if (started) throw runtime_error("Game has already started");

                if (socket.is_open()) {
                    send_start_game();
                } else {
                    map_local_to_netplay();
                    set_lag(0);
                    start_game();
                }
            } else if (params[0] == "/lag") {
                if (params.size() < 2) throw runtime_error("Missing parameter");
                uint8_t lag = stoi(params[1]);
                if (!socket.is_open()) throw runtime_error("Not connected");
                send_lag(lag);
                set_lag(lag);
            } else if (params[0] == "/autolag") {
                if (!socket.is_open()) throw runtime_error("Not connected");

                send_autolag();
            } else if (params[0] == "/my_lag") {
                if (params.size() < 2) throw runtime_error("Missing parameter");
                uint8_t lag = stoi(params[1]);
                set_lag(lag);
            } else if (params[0] == "/your_lag") {
                if (params.size() < 2) throw runtime_error("Missing parameter");
                if (!socket.is_open()) throw runtime_error("Not connected");

                uint8_t lag = stoi(params[1]);
                send_lag(lag);
            } else if (params[0] == "/golf") {
                golf = !golf;

                if (golf) {
                    my_dialog->status("Golf mode is enabled");
                } else {
                    my_dialog->status("Golf mode is disabled");
                }
            } else {
                throw runtime_error("Unknown command: " + params[0]);
            }
        } else {
            my_dialog->message(name, message);
            send_message(message);
        }
    } catch (const exception& e) {
        my_dialog->error(e.what());
    }
}

void client::set_lag(uint8_t lag) {
    this->lag = lag;

    my_dialog->status("Your lag is set to " + to_string(lag));
}

void client::remove_user(uint32_t user_id) {
    my_dialog->status(users[user_id].name + " has quit");
    users.erase(user_id);
    my_dialog->update_user_list(users);
}

void client::message_received(int32_t user_id, const string& message) {
    switch (user_id) {
        case ERROR_MESSAGE:
            my_dialog->error(message);
            break;

        case STATUS_MESSAGE:
            my_dialog->status(message);
            break;

        default:
            my_dialog->message(users[user_id].name, message);
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

void client::close() {
    connection::close();

    resolver.cancel();

    if (my_server) {
        my_server->close();
        my_server.reset();
    }

    users.clear();
    my_dialog->update_user_list(users);
}

void client::start_game() {
    unique_lock<mutex> lock(mut);
    if (started) return;

    started = true;
    start_condition.notify_all();

    my_dialog->status("Starting game...");
}

void client::handle_error(const error_code& error) {
    if (error == error::operation_aborted) return;

    close();

    for (int i = 0; i < MAX_PLAYERS; i++) {
        input_queues[i].push(BUTTONS{ 0 }); // Dummy input to unblock queues
    }

    my_dialog->error(error == error::eof ? "Disconnected from server" : error.message());
}

void client::connect(const string& host, uint16_t port, const string& room) {
    my_dialog->status("Connecting to " + host + (port == 6400 ? "" : ":" + to_string(port)) + room + "...");
    resolver.async_resolve(ip::tcp::resolver::query(host, to_string(port)), [=](const error_code& error, ip::tcp::resolver::iterator iterator) {
        if (error) return my_dialog->error(error.message());
        ip::tcp::endpoint endpoint = *iterator;
        socket.async_connect(endpoint, [=](const error_code& error) {
            if (error) {
                socket.close();
                return my_dialog->error(error.message());
            }

            error_code ec;
            socket.set_option(ip::tcp::no_delay(true), ec);
            if (ec) return my_dialog->error(ec.message());

            my_dialog->status("Connected!");

            send_join(room);

            process_packet();
        });
    });
}

void client::process_packet() {
    auto self(shared_from_this());
    read([=](packet& p) {
        if (p.size() == 0) return self->process_packet();

        switch (p.read<uint8_t>()) {
            case VERSION: {
                auto protocol_version = p.read<uint32_t>();
                if (protocol_version != PROTOCOL_VERSION) {
                    close();
                    start_game();
                    my_dialog->error("Server protocol version does not match client protocol version");
                }
                break;
            }

            case PATH: {
                path = p.read();
                my_dialog->status("Address: " + host + (port == 6400 ? "" : ":" + port) + path);
                break;
            }

            case JOIN: {
                auto user_id = p.read<uint32_t>();
                string name = p.read();
                my_dialog->status(name + " has joined");
                users[user_id].name = name;
                my_dialog->update_user_list(users);
                break;
            }

            case PING: {
                packet reply;
                reply << PONG;
                while (p.bytes_remaining()) reply << p.read<uint8_t>();
                send(reply);
                break;
            }

            case LATENCY: {
                while (p.bytes_remaining() >= sizeof(uint32_t) + sizeof(double)) {
                    auto user_id = p.read<uint32_t>();
                    auto latency = p.read<double>();
                    users[user_id].latency = latency;
                }
                my_dialog->update_user_list(users);
                break;
            }

            case NAME: {
                auto user_id = p.read<uint32_t>();
                string name = p.read();
                my_dialog->status(users[user_id].name + " is now " + name);
                users[user_id].name = name;
                my_dialog->update_user_list(users);
                break;
            }

            case QUIT: {
                auto user_id = p.read<uint32_t>();
                remove_user(user_id);
                break;
            }

            case MESSAGE: {
                auto user_id = p.read<int32_t>();
                string message = p.read();
                message_received(user_id, message);
                break;
            }

            case CONTROLLERS: {
                auto user_id = p.read<int32_t>();

                CONTROL* controllers = (user_id >= 0 ? users[user_id].controllers : netplay_controllers);
                int8_t* netplay_to_local = (user_id >= 0 ? users[user_id].control_map.netplay_to_local : my_controller_map.netplay_to_local);

                for (size_t i = 0; i < MAX_PLAYERS; i++) {
                    p >> controllers[i].Plugin;
                    p >> controllers[i].Present;
                    p >> controllers[i].RawData;
                }
                for (size_t i = 0; i < MAX_PLAYERS; i++) {
                    p >> netplay_to_local[i];
                }

                if (user_id >= 0) {
                    my_dialog->update_user_list(users);
                }

                break;
            }

            case START: {
                start_game();
                break;
            }

            case INPUT_DATA: {
                auto port = p.read<uint8_t>();
                BUTTONS buttons;
                buttons.Value = p.read<uint32_t>();
                input_queues[port].push(buttons);
                break;
            }

            case LAG: {
                lag = p.read<uint8_t>();
                break;
            }
        }

        self->process_packet();
    });
}

void client::map_local_to_netplay() {
    for (int i = 0; i < MAX_PLAYERS; i++) {
        netplay_controllers[i] = local_controllers[i];
        if (local_controllers[i].Present) {
            my_controller_map.insert(i, i);
        }
    }
}

void client::send_join(const string& room) {
    packet p;

    p << JOIN << PROTOCOL_VERSION << room << name;

    for (auto& c : local_controllers) {
        p << c.Plugin << c.Present << c.RawData;
    }

    send(p);
}

void client::send_name() {
    send(packet() << NAME << name);
}

void client::send_message(const string& message) {
    send(packet() << MESSAGE << message);
}

void client::send_controllers() {
    packet p;
    p << CONTROLLERS;
    for (auto& c : local_controllers) {
        p << c.Plugin << c.Present << c.RawData;
    }
    send(p);
}

void client::send_start_game() {
    send(packet() << START << 0);
}

void client::send_lag(uint8_t lag) {
    send(packet() << LAG << lag);
}

void client::send_autolag() {
    send(packet() << AUTOLAG);
}

void client::send_input(uint8_t port, BUTTONS input) {
    send(packet() << INPUT_DATA << port << input.Value, false);
}

void client::send_frame() {
    send(packet() << FRAME << frame);
}
