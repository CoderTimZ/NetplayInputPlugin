#include "stdafx.h"

#import "msxml6.dll" 

#include "client.h"
#include "client_dialog.h"
#include "util.h"
#include "uri.h"

using namespace std;
using namespace asio;

array<BUTTONS, 4> map_input(const array<BUTTONS, 4>& input, const controller_map& map) {
    array<BUTTONS, 4> r = { 0, 0, 0, 0 };
    array<int16_t, 4> x = { 0, 0, 0, 0 };
    array<int16_t, 4> y = { 0, 0, 0, 0 };
    auto b = map.bits;
    for (int i = 0; b && i < 4; i++) {
        for (int j = 0; b && j < 4; j++, b >>= 1) {
            if (b & 1) {
                r[j].Value |= input[i].Value;
                x[j] += input[i].X_AXIS;
                y[j] += input[i].Y_AXIS;
            }
        }
    }
    for (int j = 0; j < 4; j++) {
        r[j].X_AXIS = min(max(x[j], (int16_t)-128), (int16_t)127);
        r[j].Y_AXIS = min(max(y[j], (int16_t)-128), (int16_t)127);
    }
    return r;
}

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
                map_src_to_dst();
                start_game();
            }
        });
    });

    golf = false;

    my_dialog->status("Available Commands:\r\n\r\n"
                      "/name <name> ........... Set your name\r\n"
                      "/host [port] ........... Host a private server\r\n"
                      "/join <address> ........ Join a game\r\n"
                      "/start ................. Start the game\r\n"
                      "/autolag ............... Toggle automatic lag on and off\r\n"
                      "/lag <lag> ............. Set the netplay input lag\r\n"
                      "/golf .................. Toggle golf mode on and off\r\n");
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
                    if (p.read<uint8_t>() != VERSION) return set_ping(-3);
                    auto protocol_version = p.read<uint32_t>();
                    if (protocol_version != PROTOCOL_VERSION) return set_ping(-3);
                    c->send(packet() << PING << timestamp());
                    c->read([=](packet& p) {
                        if (p.read<uint8_t>() != PONG) return set_ping(-3);
                        set_ping(timestamp() - p.read<double>());
                    });
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

void client::set_src_controllers(CONTROL controllers[4]) {
    for (int i = 0; i < 4; i++) {
        controllers[i].RawData = FALSE; // Disallow raw data
    }

    promise<void> promise;
    io_s->post([&] {
        for (size_t i = 0; i < 4; i++) {
            src_controllers[i] = controllers[i];
        }
        send_controllers();
        promise.set_value();
    });
    promise.get_future().get();
}

void client::process_input(array<BUTTONS, 4>& input) {
    promise<void> promise;
    io_s->post([&] {
        auto& me = users[my_id];
        if (me.is_player()) {
            if (me.input_queue.size() < lag) {
                local_queue.push_back(map_input(input, me.controller_map));
                local_queue.push_back(local_queue.back());
            } else if (me.input_queue.size() == lag || frame % 2) {
                local_queue.push_back(map_input(input, me.controller_map));
            }
        } else if (golf && !saved_map.empty()) {
            if (input[0].Value || input[1].Value || input[2].Value || input[3].Value) {
                set_controller_map(saved_map);
                local_queue.push_back(map_input(input, me.controller_map));
                local_queue.push_back(local_queue.back());
            }
        }

        prepare_next_input();
        send_frame();

        promise.set_value();
    });
    promise.get_future().get();

    unique_lock<mutex> lock(next_input_mutex);
    next_input_condition.wait(lock, [=] { return this->next_input_ready; });
    input = next_input;
    next_input_ready = false;
}

void client::prepare_next_input() {
    if (flush_local_input) {
        auto& input_queue = users[my_id].input_queue;
        while (!local_queue.empty()) {
            send_input(local_queue.front());
            input_queue.push_back(local_queue.front());
            local_queue.pop_front();
        }
    }

    for (auto& e : users) {
        auto& user = e.second;
        if (user.is_player() && user.input_queue.empty()) {
            return;
        }
    }

    unique_lock<mutex> lock(next_input_mutex);
    if (next_input_ready) return;

    next_input.fill({ 0 });
    array<int16_t, 4> x = { 0, 0, 0, 0 };
    array<int16_t, 4> y = { 0, 0, 0, 0 };
    for (auto& e : users) {
        auto& user = e.second;
        if (user.input_queue.empty()) continue;
        auto& s = user.input_queue.front();
        for (int i = 0; i < 4; i++) {
            next_input[i].Value |= s[i].Value;
            x[i] += s[i].X_AXIS;
            y[i] += s[i].Y_AXIS;
        }
        user.input_queue.pop_front();
    }
    for (int i = 0; i < 4; i++) {
        next_input[i].X_AXIS = min(max(x[i], (int16_t)-128), (int16_t)127);
        next_input[i].Y_AXIS = min(max(y[i], (int16_t)-128), (int16_t)127);
    }

    frame++;

    next_input_ready = true;
    next_input_condition.notify_one();

#ifdef DEBUG
    if (!input_log.is_open()) {
        input_log.open("input.log");
    }
    for (int i = 0; i < 4; i++) {
        if (i > 0) input_log << '|';
        (next_input[i].X_AXIS ? input_log << setw(4) << setfill(' ') << showpos << next_input[i].X_AXIS << ' ' : input_log << "     ");
        (next_input[i].Y_AXIS ? input_log << setw(4) << setfill(' ') << showpos << next_input[i].Y_AXIS << ' ' : input_log << "     ");
        input_log << (next_input[i].L_TRIG ? 'L' : ' ');
        input_log << (next_input[i].R_TRIG ? 'R' : ' ');
        input_log << (next_input[i].U_CBUTTON ? '^' : ' ');
        input_log << (next_input[i].D_CBUTTON ? 'v' : ' ');
        input_log << (next_input[i].L_CBUTTON ? '<' : ' ');
        input_log << (next_input[i].R_CBUTTON ? '>' : ' ');
        input_log << (next_input[i].A_BUTTON ? 'A' : ' ');
        input_log << (next_input[i].B_BUTTON ? 'B' : ' ');
        input_log << (next_input[i].Z_TRIG ? 'Z' : ' ');
        input_log << (next_input[i].START_BUTTON ? 'S' : ' ');
        input_log << (next_input[i].U_DPAD ? '^' : ' ');
        input_log << (next_input[i].D_DPAD ? 'v' : ' ');
        input_log << (next_input[i].L_DPAD ? '<' : ' ');
        input_log << (next_input[i].R_DPAD ? '>' : ' ');
    }
    input_log << '\n';
#endif
}

void client::set_dst_controllers(CONTROL dst_controllers[4]) {
    promise<void> promise;
    io_s->post([&] {
        this->dst_controllers = dst_controllers;
        promise.set_value();
    });
    promise.get_future().get();
}

void client::post_close() {
    io_s->post([&] {
        close();
        map_src_to_dst();
        start_game();
    });
}

void client::wait_until_start() {
    if (started) return;
    unique_lock<mutex> lock(start_mutex);
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
                    map_src_to_dst();
                    set_lag(0);
                    start_game();
                }
            } else if (params[0] == "/lag") {
                if (params.size() < 2) throw runtime_error("Missing parameter");
                uint8_t lag = stoi(params[1]);
                if (!socket.is_open()) throw runtime_error("Not connected");
                
                send_autolag(0);
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
                if (!my_id) throw runtime_error("Not connected to server");
                golf = !golf;
                send(packet() << GOLF << (uint8_t)golf);
                if (golf) {
                    my_dialog->status("Golf mode is enabled");
                } else {
                    set_controller_map(saved_map);
                    my_dialog->status("Golf mode is disabled");
                }
            } else if (params[0] == "/map") {
                if (!my_id) throw runtime_error("Not connected to server");
                auto& me = users[my_id];
                controller_map map;
                for (size_t i = 2; i < params.size(); i += 2) {
                    int src = stoi(params[i - 1]) - 1;
                    int dst = stoi(params[i]) - 1;
                    map.set(src, dst);
                }
                set_controller_map(map);
                saved_map = map;
            } else {
                throw runtime_error("Unknown command: " + params[0]);
            }
        } else {
            my_dialog->message(name, message);
            send_message(message);
        }
    } catch (const exception& e) {
        my_dialog->error(e.what());
    } catch (const error_code& e) {
        my_dialog->error(e.message());
    }
}

void client::set_lag(uint8_t lag) {
    this->lag = lag;

    my_dialog->set_lag(lag);
}

void client::remove_user(uint32_t user_id) {
    my_dialog->status(users[user_id].name + " has quit");
    users.erase(user_id);
    update_user_list();
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
            if (users.find(user_id) == users.end()) break;
            my_dialog->message(users[user_id].name, message);
    }
}

uint8_t client::get_total_count() {
    uint8_t count = 0;

    for (int i = 0; i < 4; i++) {
        if (dst_controllers[i].Present) {
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

    for (auto it = users.begin(); it != users.end();) {
        it = (it->first == my_id ? ++it : users.erase(it));
    }

    update_user_list();
    
    if (!users.empty()) {
        prepare_next_input();
    }
}

void client::start_game() {
    unique_lock<mutex> lock(start_mutex);
    if (started) return;
    started = true;
    start_condition.notify_all();
    my_dialog->status("Starting game...");
}

void client::handle_error(const error_code& error) {
    if (error == error::operation_aborted) return;
    close();
    my_dialog->error(error == error::eof ? "Disconnected from server" : error.message());
}

void client::connect(const string& host, uint16_t port, const string& room) {
    my_dialog->status("Connecting to " + host + (port == 6400 ? "" : ":" + to_string(port)) + "...");
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
        if (p.empty()) return self->process_packet();

        try {
            switch (p.read<uint8_t>()) {
                case VERSION: {
                    auto protocol_version = p.read<uint32_t>();
                    if (protocol_version != PROTOCOL_VERSION) {
                        close();
                        start_game();
                        my_dialog->error("Server protocol version does not match client protocol version. Visit www.play64.com to get the latest version of the plugin.");
                    }
                    break;
                }

                case ACCEPT: {
                    users.clear();
                    p >> my_id;
                    break;
                }

                case PATH: {
                    path = p.read();
                    my_dialog->status(
                        "Others can join with the following command:\r\n\r\n"
                        "/join " + (host == "127.0.0.1" ? "<Your IP Address>" : host) + (port == 6400 ? "" : ":" + to_string(port)) + (path == "/" ? "" : path) + "\r\n"
                    );
                    break;
                }

                case JOIN: {
                    auto user_id = p.read<uint32_t>();
                    if (users.find(user_id) != users.end()) break;
                    string name = p.read();
                    my_dialog->status(name + " has joined");
                    users[user_id].name = name;
                    update_user_list();
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
                    while (p.bytes_remaining()) {
                        auto it = users.find(p.read<uint32_t>());
                        if (it == users.end()) break;
                        auto& user = it->second;
                        auto latency = p.read<double>();
                        user.latency = latency;
                    }
                    update_user_list();
                    break;
                }

                case NAME: {
                    auto it = users.find(p.read<uint32_t>());
                    if (it == users.end()) break;
                    auto& user = it->second;
                    string name = p.read();
                    my_dialog->status(user.name + " is now " + name);
                    user.name = name;
                    update_user_list();
                    break;
                }

                case QUIT: {
                    auto user_id = p.read<uint32_t>();
                    if (users.find(user_id) == users.end()) break;
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
                    while (p.bytes_remaining()) {
                        auto user_id = p.read<uint32_t>();
                        auto it = users.find(user_id);
                        if (it == users.end()) break;
                        auto& user = it->second;
                        for (size_t i = 0; i < 4; i++) {
                            p >> user.controllers[i];
                        }
                        p >> user.controller_map;
                        if (user_id == my_id) {
                            saved_map = user.controller_map;
                        }
                    }
                    
                    if (!started) {
                        for (uint8_t j = 0; j < 4; j++) {
                            dst_controllers[j].Present = 1;
                            dst_controllers[j].RawData = 0;
                            dst_controllers[j].Plugin = PLUGIN_NONE;
                            for (uint8_t i = 0; i < 4; i++) {
                                for (auto& e : users) {
                                    if (e.second.controller_map.get(i, j)) {
                                        dst_controllers[j].Plugin = max(dst_controllers[j].Plugin, e.second.controllers[i].Plugin);
                                    }
                                }
                            }
                        }
                    }

                    update_user_list();
                    break;
                }

                case START: {
                    start_game();
                    break;
                }

                case INPUT_DATA: {
                    auto user_id = p.read<uint32_t>();
                    auto it = users.find(user_id);
                    if (it == users.end()) break;
                    auto& user = it->second;
                    array<BUTTONS, 4> input;
                    for (int i = 0; i < 4; i++) {
                        p >> input[i];
                    }
                    user.input_queue.push_back(input);
                    prepare_next_input();
                    break;
                }

                case LAG: {
                    lag = p.read<uint8_t>();
                    my_dialog->set_lag(lag);
                    break;
                }

                case CONTROLLER_MAP: {
                    auto user_id = p.read<uint32_t>();
                    auto it = users.find(user_id);
                    if (it == users.end()) break;
                    auto map = p.read<controller_map>();
                    auto& user = it->second;
                    if (user.controller_map.bits == map.bits) break;
                    if (user.controller_map.empty()) {
                        send(packet() << FRAME_COUNT << user_id << (uint32_t)(frame + user.input_queue.size()));
                    }
                    user.controller_map = map;
                    if (golf && user_id != my_id && !map.empty()) {
                        controller_map empty;
                        set_controller_map(empty);
                    }
                    update_user_list();
                    prepare_next_input();
                    break;
                }

                case GOLF: {
                    golf = p.read<uint8_t>();
                    if (golf) {
                        controller_map empty;
                        set_controller_map(empty);
                        my_dialog->status("Golf mode is enabled");
                    } else {
                        set_controller_map(saved_map);
                        my_dialog->status("Golf mode is disabled");
                    }
                    break;
                }

                case FLUSH_LOCAL_INPUT: {
                    flush_local_input = true;
                    prepare_next_input();
                }
            }

            self->process_packet();
        } catch (const exception& e) {
            my_dialog->error(e.what());
            self->close();
        } catch (const error_code& e) {
            my_dialog->error(e.message());
            self->close();
        } catch (...) {
            self->close();
        }
    });
}

void client::map_src_to_dst() {
    users[my_id].controller_map.clear();
    for (int i = 0; i < 4; i++) {
        dst_controllers[i] = src_controllers[i];
        if (src_controllers[i].Present) {
            users[my_id].controller_map.set(i, i);
        }
    }
}

void client::update_user_list() {
    vector<string> lines;
    lines.reserve(users.size());

    for (auto& e : users) {
        auto& data = e.second;
        string line = "[";
        for (int j = 0; j < 4; j++) {
            if (j > 0) line += " ";

            int i;
            for (i = 0; i < 4 && !data.controller_map.get(i, j); i++);

            if (i == 4) {
                line += "- ";
            } else {
                line += to_string(i + 1);
                switch (data.controllers[i].Plugin) {
                    case PLUGIN_MEMPAK: line += "M"; break;
                    case PLUGIN_RUMBLE_PAK: line += "R"; break;
                    case PLUGIN_TANSFER_PAK: line += "T"; break;
                    default: line += " ";
                }
            }
        }
        line += "] ";
        line += data.name;
        if (!isnan(data.latency)) {
            line += " (" + to_string((int)(data.latency * 1000)) + " ms)";
        }

        lines.push_back(line);
    }

    my_dialog->update_user_list(lines);
}

void client::set_controller_map(controller_map map) {
    if (!my_id) return;

    auto& me = users[my_id];
    if (me.controller_map.bits == map.bits) return;

    send_controller_map(map);

    if (me.controller_map.empty()) {
        flush_local_input = false;
        send(packet() << FRAME_COUNT << my_id << (uint32_t)(frame + me.input_queue.size()));
    }
    me.controller_map = map;

    update_user_list();
    prepare_next_input();
}

void client::send_join(const string& room) {
    packet p;
    p << JOIN << PROTOCOL_VERSION << room << name;
    for (auto& c : src_controllers) {
        p << c;
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
    for (auto& c : src_controllers) {
        p << c;
    }
    send(p);
}

void client::send_start_game() {
    send(packet() << START);
}

void client::send_lag(uint8_t lag) {
    send(packet() << LAG << lag);
}

void client::send_autolag(int8_t value) {
    send(packet() << AUTOLAG << value);
}

void client::send_input(const array<BUTTONS, 4>& input) {
    packet p;
    p << INPUT_DATA;
    for (uint8_t i = 0; i < 4; i++) {
        p << input[i];
    }
    send(p, false);
}

void client::send_frame() {
    send(packet() << FRAME);
}

void client::send_controller_map(controller_map map) {
    send(packet() << CONTROLLER_MAP << map);
}
