#include "stdafx.h"

#import "msxml6.dll"

#include "client.h"
#include "client_dialog.h"
#include "util.h"
#include "uri.h"

using namespace std;
using namespace asio;

bool operator==(const BUTTONS& lhs, const BUTTONS& rhs) {
    return lhs.Value == rhs.Value;
}

bool operator!=(const BUTTONS& lhs, const BUTTONS& rhs) {
    return !(lhs == rhs);
}

client::client(shared_ptr<io_service> io_service, shared_ptr<client_dialog> dialog)
    : connection(io_service), my_dialog(dialog), work(*io_s), resolver(*io_s), thread([&] { io_s->run(); }) {

    my_dialog->set_message_handler([=](string message) {
        io_s->post([=] { process_message(message); });
    });

    my_dialog->set_close_handler([=] {
        io_s->post([=] {
            if (started) {
                my_dialog->minimize();
            } else {
                my_dialog->destroy();
                close();
                map_src_to_dst();
                start_game();
            }
        });
    });

    my_dialog->status("Available Commands:\r\n\r\n"
                      "/name <name> ........... Set your name\r\n"
                      "/host [port] ........... Host a private server\r\n"
                      "/join <address> ........ Join a game\r\n"
                      "/hia [input rate] ...... Toggle host input authority mode\r\n"
                      "/start ................. Start the game\r\n"
                      "/autolag ............... Toggle automatic lag on and off\r\n"
                      "/lag <lag> ............. Set the netplay input lag\r\n"
                      "/golf .................. Toggle golf mode\r\n");

#ifdef DEBUG
    input_log.open("input.log");
#endif
}

client::~client() {
    if (thread.get_id() != this_thread::get_id()) {
        io_s->stop();
        thread.join();
    } else {
        thread.detach();
    }
}

template<typename F> auto client::run(F&& f) {
    packaged_task<decltype(f())(void)> task(f);
    io_s->post([&] { task(); });
    return task.get_future().get();
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
                c->receive([=](packet& pin) {
                    if (pin.empty() || pin.read<PACKET_TYPE>() != VERSION) return set_ping(-3);
                    auto protocol_version = pin.read<uint32_t>();
                    if (protocol_version != PROTOCOL_VERSION) return set_ping(-3);
                    c->send(pout.reset() << PING << timestamp());
                    c->receive([=](packet& pin) {
                        if (pin.empty() || pin.read<PACKET_TYPE>() != PONG) return set_ping(-3);
                        set_ping(timestamp() - pin.read<double>());
                    });
                });
            });
        });
    }
}

string client::get_name() {
    return run([&] { return name; });
}

void client::set_name(const string& name) {
    run([&] {
        this->name = name;
        my_dialog->status("Your name is " + name);
    });
}

void client::set_src_controllers(CONTROL controllers[4]) {
    for (int i = 0; i < 4; i++) {
        controllers[i].RawData = FALSE; // Disallow raw data
    }

    run([&] {
        for (int i = 0; i < 4; i++) {
            src_controllers[i] = controllers[i];
        }
        send_controllers();
    });
}

void client::process_input(array<BUTTONS, 4>& input) {
    run([&] {
#ifdef DEBUG
        //static uniform_int_distribution<uint32_t> dist(16, 63);
        //static random_device rd;
        //static uint32_t f = 0;
        //while (frame >= f) f += dist(rd);
        //if (golf) input[0].A_BUTTON = (f & 1);
#endif
        auto& me = users[my_id];
        if (hia && socket.is_open()) {
            send_input(input);
        } else {
            if (me.is_player()) {
                if (me.input_queue.size() < lag) {
                    local_queue.push_back(input);
                    local_queue.push_back(input);
                } else if (me.input_queue.size() == lag || frame % 2) {
                    local_queue.push_back(input);
                }
            } else if (golf && input != EMPTY_INPUT) {
                set_controller_map(golf_map);
                local_queue.push_back(input);
                local_queue.push_back(input);
            }
        }
        on_input();
    });

    unique_lock<mutex> lock(next_input_mutex);
    next_input_condition.wait(lock, [=] { return !next_input.empty(); });
    input = next_input.front();
    next_input.pop_front();
}

void client::on_input() {
    if (syncing) return;

    auto& queue = users[my_id].input_queue;
    while (!local_queue.empty()) {
        queue.push_back(make_pair(local_queue.front(), users[my_id].control_map));
        send_input(local_queue.front());
        local_queue.pop_front();
    }

    for (auto& e : users) {
        auto& u = e.second;
        if (u.is_player() && u.input_queue.empty()) {
            return flush();
        }
    }

    unique_lock<mutex> lock(next_input_mutex);
    if (!next_input.empty()) return flush();

    array<BUTTONS, 4> dst = { 0, 0, 0, 0 };
    array<int, 4> x_axis = { 0, 0, 0, 0 };
    array<int, 4> y_axis = { 0, 0, 0, 0 };
    for (auto& e : users) {
        auto& user = e.second;
        if (user.input_queue.empty()) continue;
        auto& src = user.input_queue.front().first;
        auto b = user.input_queue.front().second.bits;
        for (int i = 0; b && i < 4; i++) {
            for (int j = 0; b && j < 4; j++, b >>= 1) {
                if (b & 1) {
                    dst[j].Value |= src[i].Value;
                    x_axis[j] += src[i].X_AXIS;
                    y_axis[j] += src[i].Y_AXIS;
                }
            }
        }
        user.input_queue.pop_front();
    }
    for (int j = 0; j < 4; j++) {
        double x = x_axis[j] + 0.5;
        double y = y_axis[j] + 0.5;
        double r = max(abs(x), abs(y));
        if (r > 127.5) {
            dst[j].X_AXIS = (int)floor(x / r * 127.5);
            dst[j].Y_AXIS = (int)floor(y / r * 127.5);
        } else {
            dst[j].X_AXIS = x_axis[j];
            dst[j].Y_AXIS = y_axis[j];
        }
    }

    next_input.push_back(dst);
    next_input_condition.notify_one();

    frame++;
    if (users[my_id].is_player()) {
        send_frame();
    }

    flush();

#ifdef DEBUG
    const static string B = "><v^SZBA><v^RL";
    static array<BUTTONS, 4> prev = EMPTY_INPUT;
    if (input_log.is_open() && dst != prev) {
        input_log << setw(8) << setfill('0') << frame << '|';
        for (int i = 0; i < 4; i++) {
            (dst[i].X_AXIS ? input_log << setw(4) << setfill(' ') << showpos << dst[i].X_AXIS << ' ' : input_log << "     ");
            (dst[i].Y_AXIS ? input_log << setw(4) << setfill(' ') << showpos << dst[i].Y_AXIS << ' ' : input_log << "     ");
            for (int j = B.length() - 1; j >= 0; j--) {
                input_log << (dst[i].Value & (1 << j) ? B[j] : ' ');
            }
            input_log << '|';
        }
        input_log << '\n';
    }
    prev = dst;
#endif
}

void client::set_dst_controllers(CONTROL dst_controllers[4]) {
    run([&] { this->dst_controllers = dst_controllers; });
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
            } else if (params[0] == "/hia") {
                if (!socket.is_open()) throw runtime_error("Not connected");
                uint32_t new_hia = (params.size() == 2 ? stoi(params[1]) : (hia ? 0 : 60));
                if (!started || hia && new_hia) {
                    send_hia(new_hia);
                } else {
                    throw runtime_error("Game has already started");
                }
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
                if (hia) throw runtime_error("This setting has no effect when host input authority mode is enabled");
                
                send_autolag(0);
                send_lag(lag);
                set_lag(lag);
            } else if (params[0] == "/autolag") {
                if (!socket.is_open()) throw runtime_error("Not connected");
                if (hia) throw runtime_error("This setting has no effect when host input authority mode is enabled");

                send_autolag();
            } else if (params[0] == "/my_lag") {
                if (params.size() < 2) throw runtime_error("Missing parameter");
                if (hia) throw runtime_error("This setting has no effect when host input authority mode is enabled");

                uint8_t lag = stoi(params[1]);
                set_lag(lag);
            } else if (params[0] == "/your_lag") {
                if (params.size() < 2) throw runtime_error("Missing parameter");
                if (!socket.is_open()) throw runtime_error("Not connected");
                if (hia) throw runtime_error("This setting has no effect when host input authority mode is enabled");

                uint8_t lag = stoi(params[1]);
                send_lag(lag);
            } else if (params[0] == "/golf") {
                if (!my_id) throw runtime_error("Not connected");
                if (hia) throw runtime_error("This setting has no effect when host input authority mode is enabled");

                golf = !golf;
                send(pout.reset() << GOLF << golf);
                if (golf) {
                    golf_map = users[my_id].control_map;
                    my_dialog->status("Golf mode is enabled");
                } else {
                    set_controller_map(golf_map);
                    my_dialog->status("Golf mode is disabled");
                }
            } else if (params[0] == "/map") {
                if (!my_id) throw runtime_error("Not connected");

                controller_map map;
                for (size_t i = 2; i < params.size(); i += 2) {
                    int src = stoi(params[i - 1]) - 1;
                    int dst = stoi(params[i]) - 1;
                    map.set(src, dst);
                }
                set_controller_map(map);
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
    sync();
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
    
    // Prevent deadlock
    users[my_id].input_queue.push_back(make_pair(EMPTY_INPUT, EMPTY_MAP));
    on_input();
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
    receive([=](packet& pin) {
        if (pin.empty()) return self->process_packet();

        try {
            switch (pin.read<PACKET_TYPE>()) {
                case VERSION: {
                    auto protocol_version = pin.read<uint32_t>();
                    if (protocol_version != PROTOCOL_VERSION) {
                        close();
                        my_dialog->error("Server protocol version does not match client protocol version. Visit www.play64.com to get the latest version of the plugin.");
                    }
                    break;
                }

                case ACCEPT: {
                    users.clear();
                    pin >> my_id;
                    break;
                }

                case PATH: {
                    path = pin.read();
                    my_dialog->status(
                        "Others may join with the following command:\r\n\r\n"
                        "/join " + (host == "127.0.0.1" ? "<Your IP Address>" : host) + (port == 6400 ? "" : ":" + to_string(port)) + (path == "/" ? "" : path) + "\r\n"
                    );
                    break;
                }

                case JOIN: {
                    auto user_id = pin.read<uint32_t>();
                    string name = pin.read();
                    my_dialog->status(name + " has joined");
                    users[user_id].name = name;
                    update_user_list();
                    break;
                }

                case PING: {
                    pout.reset() << PONG;
                    while (pin.available()) {
                        pout << pin.read<uint8_t>();
                    }
                    send(pout);
                    break;
                }

                case LATENCY: {
                    while (pin.available()) {
                        auto user_id = pin.read<uint32_t>();
                        auto it = users.find(user_id);
                        if (it == users.end()) break;
                        auto& user = it->second;
                        user.latency = pin.read<double>();
                        if (hia && user_id == my_id) {
                            my_dialog->set_latency(user.latency);
                        }
                    }
                    update_user_list();
                    break;
                }

                case NAME: {
                    auto it = users.find(pin.read<uint32_t>());
                    if (it == users.end()) break;
                    auto& user = it->second;
                    string name = pin.read();
                    my_dialog->status(user.name + " is now " + name);
                    user.name = name;
                    update_user_list();
                    break;
                }

                case QUIT: {
                    auto user_id = pin.read<uint32_t>();
                    if (users.find(user_id) == users.end()) break;
                    remove_user(user_id);
                    break;
                }

                case MESSAGE: {
                    auto user_id = pin.read<int32_t>();
                    string message = pin.read();
                    message_received(user_id, message);
                    break;
                }

                case CONTROLLERS: {
                    while (pin.available()) {
                        auto user_id = pin.read<uint32_t>();
                        auto it = users.find(user_id);
                        if (it == users.end()) break;
                        auto& user = it->second;
                        for (int i = 0; i < 4; i++) {
                            auto& c = user.controllers[i];
                            pin >> c.Plugin >> c.Present >> c.RawData;
                        }
                        pin >> user.control_map.bits;
                    }
                    
                    if (!started) {
                        for (int j = 0; j < 4; j++) {
                            dst_controllers[j].Present = 1;
                            dst_controllers[j].RawData = 0;
                            dst_controllers[j].Plugin = PLUGIN_NONE;
                            for (int i = 0; i < 4; i++) {
                                for (auto& e : users) {
                                    if (e.second.control_map.get(i, j)) {
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
                    auto user_id = pin.read<uint32_t>();
                    auto it = users.find(user_id);
                    if (it == users.end()) break;
                    auto& user = it->second;
                    array<BUTTONS, 4> input = { 0, 0, 0, 0 };
                    while (pin.available()) {
                        auto player = pin.read<uint8_t>();
                        auto buttons = pin.read<DWORD>();
                        if (player < input.size()) {
                            input[player].Value = buttons;
                        }
                    }
                    user.input_queue.push_back(make_pair(input, user.control_map));
                    on_input();
                    break;
                }

                case LAG: {
                    lag = pin.read<uint8_t>();
                    my_dialog->set_lag(lag);
                    break;
                }

                case CONTROLLER_MAP: {
                    auto user_id = pin.read<uint32_t>();
                    auto it = users.find(user_id);
                    if (it == users.end()) break;
                    controller_map map(pin.read<uint16_t>());
                    auto& user = it->second;
                    user.control_map = map;
                    auto& me = users[my_id];
                    if (golf && !hia && user.is_player() && me.is_player()) {
                        golf_map = me.control_map;
                        set_controller_map({ 0 });
                    }
                    update_user_list();
                    on_input();
                    break;
                }

                case SYNC_REQ: {
                    auto user_id = pin.read<uint32_t>();
                    auto it = users.find(user_id);
                    if (it == users.end()) break;
                    auto& user = it->second;
                    auto sync_id = pin.read<uint32_t>();
                    send(pout.reset() << SYNC_RES << user_id << sync_id << static_cast<uint32_t>(frame + user.input_queue.size()));
                    break;
                }

                case SYNC_RES: {
                    auto user_id = pin.read<uint32_t>();
                    auto it = users.find(user_id);
                    if (it == users.end()) break;
                    auto& user = it->second;
                    user.sync_id = pin.read<uint32_t>();
                    user.sync_frame = pin.read<uint32_t>();
                    sync();
                    break;
                }

                case INPUT_FILL: {
                    auto user_id = pin.read<uint32_t>();
                    auto it = users.find(user_id);
                    if (it == users.end()) break;
                    auto& user = it->second;
                    auto end_frame = pin.read<uint32_t>();
                    while (frame + user.input_queue.size() < end_frame) {
                        user.input_queue.push_back(make_pair(EMPTY_INPUT, EMPTY_MAP));
                    }
                    on_input();
                    break;
                }

                case GOLF: {
                    golf = pin.read<uint8_t>();
                    if (hia) break;
                    if (golf) {
                        golf_map = users[my_id].control_map;
                        set_controller_map({ 0 });
                        my_dialog->status("Golf mode is enabled");
                    } else {
                        set_controller_map(golf_map);
                        my_dialog->status("Golf mode is disabled");
                    }
                    break;
                }

                case HIA: {
                    auto new_hia = pin.read_var<uint32_t>();
                    if (!started || hia && new_hia) {
                        set_hia(new_hia);
                    }
                    break;
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
    users[my_id].control_map.clear();
    for (int i = 0; i < 4; i++) {
        dst_controllers[i] = src_controllers[i];
        if (src_controllers[i].Present) {
            users[my_id].control_map.set(i, i);
        }
    }
}

void client::update_user_list() {
    vector<string> lines;
    lines.reserve(users.size());

    for (auto& e : users) {
        auto& u = e.second;
        string line = "[";
        for (int j = 0; j < 4; j++) {
            if (j > 0) line += " ";

            int i;
            for (i = 0; i < 4 && !u.control_map.get(i, j); i++);

            if (i == 4) {
                line += "- ";
            } else {
                line += to_string(i + 1);
                switch (u.controllers[i].Plugin) {
                    case PLUGIN_MEMPAK: line += "M"; break;
                    case PLUGIN_RUMBLE_PAK: line += "R"; break;
                    case PLUGIN_TANSFER_PAK: line += "T"; break;
                    default: line += " ";
                }
            }
        }
        line += "] ";
        line += u.name;
        if (!isnan(u.latency)) {
            line += " (" + to_string((int)(u.latency * 1000)) + " ms)";
        }

        lines.push_back(line);
    }

    my_dialog->update_user_list(lines);

    update_frame_limit();
}

void client::update_frame_limit() {
    if (my_dialog->is_emulator_project64z()) {
        if (hia) {
            if (socket.is_open()) {
                if (frame_limit) {
                    PostMessage(my_dialog->get_emulator_window(), WM_COMMAND, ID_SYSTEM_LIMITFPS_OFF, 0);
                    frame_limit = false;
                }
            } else {
                if (!frame_limit) {
                    PostMessage(my_dialog->get_emulator_window(), WM_COMMAND, ID_SYSTEM_LIMITFPS_ON, 0);
                    frame_limit = true;
                }
            }
        } else if (my_id) {
            if (users[my_id].is_player() || find_if(users.begin(), users.end(), [](auto& e) { return e.second.is_player(); }) == users.end()) {
                if (!frame_limit) {
                    PostMessage(my_dialog->get_emulator_window(), WM_COMMAND, ID_SYSTEM_LIMITFPS_ON, 0);
                    frame_limit = true;
                }
            } else {
                if (frame_limit) {
                    PostMessage(my_dialog->get_emulator_window(), WM_COMMAND, ID_SYSTEM_LIMITFPS_OFF, 0);
                    frame_limit = false;
                }
            }
        }
    }
}

void client::set_controller_map(controller_map new_map) {
    if (!my_id) return;

    auto& me = users[my_id];
    if (me.control_map.bits == new_map.bits) return;

    bool was_player = me.is_player();

    me.control_map = new_map;
    send_controller_map(me.control_map);

    if (!was_player && me.is_player() && users.size() > 1) {
        syncing = true;
        me.sync_id++;
        me.sync_frame = frame + me.input_queue.size();
        send(pout.reset() << SYNC_REQ << me.sync_id);
    } else if (was_player && !me.is_player() && syncing) {
        syncing = false;
        me.sync_id++;
        local_queue.clear();
    }

    update_user_list();
    on_input();
}

void client::sync() {
    if (!syncing) return;

    auto& me = users[my_id];
    if (all_of(users.begin(), users.end(), [&](auto& e) { return e.second.sync_id == me.sync_id; })) {
        syncing = false;
        send_controller_map(me.control_map);

        auto end_frame = max_element(users.begin(), users.end(), [](auto& a, auto& b) {
            return a.second.sync_frame < b.second.sync_frame;
        })->second.sync_frame + 1;

        send(pout.reset() << INPUT_FILL << end_frame);
        while (frame + me.input_queue.size() < end_frame) {
            me.input_queue.push_back(make_pair(EMPTY_INPUT, EMPTY_MAP));
        }

        update_user_list();
        on_input();
    }
}

void client::set_hia(uint32_t hia) {
    if (this->hia == hia) return;

    this->hia = hia;

    if (hia) {
        if (my_dialog->is_emulator_project64z()) {
            update_frame_limit();
            my_dialog->status("Host input authority is enabled at " + to_string(hia) + " Hz");
        } else {
            my_dialog->status("Host input authority is enabled at " + to_string(hia) + " Hz\r\n\r\n===>  Please disable your emulator's frame rate limit setting  <===\r\n");
        }
    } else {
        if (my_dialog->is_emulator_project64z()) {
            update_frame_limit();
            my_dialog->status("Host input authority is disabled");
        } else {
            my_dialog->status("Host input authority is disabled\r\n\r\n===>  Please enable your emulator's frame rate limit setting  <===\r\n");
        }
    }
}

void client::send_join(const string& room) {
    pout.reset() << JOIN << PROTOCOL_VERSION << room << name;
    for (auto& c : src_controllers) {
        pout << c.Plugin << c.Present << c.RawData;
    }
    send(pout);
}

void client::send_name() {
    send(pout.reset() << NAME << name);
}

void client::send_message(const string& message) {
    send(pout.reset() << MESSAGE << message);
}

void client::send_controllers() {
    pout.reset() << CONTROLLERS;
    for (auto& c : src_controllers) {
        pout << c.Plugin << c.Present << c.RawData;
    }
    send(pout);
}

void client::send_start_game() {
    send(pout.reset() << START);
}

void client::send_lag(uint8_t lag) {
    send(pout.reset() << LAG << lag);
}

void client::send_autolag(int8_t value) {
    send(pout.reset() << AUTOLAG << value);
}

void client::send_input(const array<BUTTONS, 4>& input) {
    pout.reset() << INPUT_DATA;
    for (uint8_t i = 0; i < 4; i++) {
        if (input[i].Value) {
            pout << i << input[i].Value;
        }
    }
    send(pout, false);
}

void client::send_frame() {
    send(pout.reset() << FRAME, false);
}

void client::send_controller_map(controller_map map) {
    send(pout.reset() << CONTROLLER_MAP << map.bits);
}

void client::send_hia(uint32_t hia) {
    pout.reset() << HIA;
    pout.write_var(hia);
    send(pout);
}