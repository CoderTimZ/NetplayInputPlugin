#include "stdafx.h"

#include "client.h"
#include "client_dialog.h"
#include "connection.h"
#include "util.h"
#include "uri.h"

using namespace std;
using namespace asio;

int get_input_rate(char code) {
    switch (code) {
        case BRAZILIAN:
        case CHINESE:
        case GERMAN:
        case FRENCH:
        case DUTCH:
        case ITALIAN:
        case GATEWAY_64_PAL:
        case EUROPEAN_BASIC_SPEC:
        case SPANISH:
        case AUSTRALIAN:
        case SCANDINAVIAN:
        case EUROPEAN_X:
        case EUROPEAN_Y:
            return 50;
        default:
            return 60;
    }
}

bool operator==(const BUTTONS& lhs, const BUTTONS& rhs) {
    return lhs.Value == rhs.Value;
}

bool operator!=(const BUTTONS& lhs, const BUTTONS& rhs) {
    return !(lhs == rhs);
}

client::client(shared_ptr<client_dialog> dialog) :
    connection(service), timer(service), my_dialog(dialog)
{
    my_dialog->set_message_handler([=](string message) {
        service.post([=] { on_message(message); });
    });

    my_dialog->set_close_handler([=] {
        service.post([=] {
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

    my_dialog->info("Available Commands:\r\n\r\n"
                    "/name <name>		Set your name\r\n"
                    "/host [port]		Host a private server\r\n"
                    "/join <address>		Join a game\r\n"
                    "/start			Start the game\r\n"
                    "/map <src>:<dst> [...]	Map your controller ports\r\n"
                    "/autolag			Toggle automatic lag on and off\r\n"
                    "/lag <lag>		Set the netplay input lag\r\n"
                    "/golf			Toggle golf mode on and off\r\n"
                    "/auth <id>		Delegate input authority to another user\r\n");

#ifdef DEBUG
    input_log.open("input.log");
#endif
}

client::~client() {
    stop();
}

void client::on_error(const error_code& error) {
    if (error) {
        my_dialog->error(error == error::eof ? "Disconnected from server" : error.message());
    }
}

bool client::input_detected(const input_data& input, uint32_t mask) {
    for (auto b : reinterpret_cast<const std::array<BUTTONS, 4>&>(input.data)) {
        b.Value &= mask;
        if (b.X_AXIS <= -16 || b.X_AXIS >= +16) return true;
        if (b.Y_AXIS <= -16 || b.Y_AXIS >= +16) return true;
        if (b.Value & 0x0000FFFF) return true;
    }
    return false;
}

void client::load_public_server_list() {
    constexpr static char API_HOST[] = "api.play64.com";

    service.post([&] {
        ip::tcp::resolver tcp_resolver(service);
        error_code error;
        auto iterator = tcp_resolver.resolve(API_HOST, "80", error);
        if (error) return my_dialog->error("Failed to load server list");
        auto s = make_shared<ip::tcp::socket>(service);
        s->async_connect(*iterator, [=](const error_code& error) {
            if (error) return my_dialog->error("Failed to load server list");
            shared_ptr<string> buf = make_shared<string>(
                "GET /servers.txt HTTP/1.1\r\n"
                "Host: " + string(API_HOST) + "\r\n"
                "Connection: close\r\n\r\n");
            async_write(*s, buffer(*buf), [=](const error_code& error, size_t transferred) {
                if (error) {
                    s->close();
                    my_dialog->error("Failed to load server list");
                } else {
                    buf->resize(4096);
                    async_read(*s, buffer(*buf), [=](const error_code& error, size_t transferred) {
                        s->close();
                        if (error != error::eof) return my_dialog->error("Failed to load server list");
                        buf->resize(transferred);
                        public_servers.clear();
#ifdef DEBUG
                        public_servers["localhost"] = SERVER_STATUS_PENDING;
#endif
                        bool content = false;
                        for (size_t start = 0, end = 0; end != string::npos; start = end + 1) {
                            end = buf->find('\n', start);
                            string line = buf->substr(start, end == string::npos ? string::npos : end - start);
                            if (!line.empty() && line.back() == '\r') {
                                line.resize(line.length() - 1);
                            }
                            if (line.empty()) {
                                content = true;
                            } else if (content) {
                                public_servers[line] = SERVER_STATUS_PENDING;
                            }
                        }
                        my_dialog->update_server_list(public_servers);
                        ping_public_server_list();
                    });
                }
            });
        });
    });
}

void client::ping_public_server_list() {
    auto done = [=](const string& host, double ping, shared_ptr<ip::udp::socket> socket = nullptr) {
        public_servers[host] = ping;
        my_dialog->update_server_list(public_servers);
        if (socket && socket->is_open()) {
            socket->close();
        }
    };
    auto s(weak_from_this());
    for (auto& e : public_servers) {
        uri u(e.first.substr(0, e.first.find('|')));
        udp_resolver.async_resolve(u.host, to_string(u.port ? u.port : 6400), [=](const auto& error, auto iterator) {
            if (s.expired()) return;
            if (error) return done(e.first, SERVER_STATUS_ERROR);
            auto socket = make_shared<ip::udp::socket>(service);
            socket->open(iterator->endpoint().protocol());
            socket->connect(*iterator);
            auto p(make_shared<packet>());
            *p << SERVER_PING << timestamp();
            socket->async_send(buffer(*p), [=](const error_code& error, size_t transferred) {
                if (s.expired()) return;
                p->reset();
                if (error) return done(e.first, SERVER_STATUS_ERROR, socket);

                auto timer = make_shared<asio::steady_timer>(service);
                timer->expires_after(std::chrono::seconds(3));
                socket->async_wait(ip::udp::socket::wait_read, [=](const error_code& error) {
                    if (s.expired()) return;
                    timer->cancel();
                    if (error) return done(e.first, SERVER_STATUS_ERROR, socket);
                    error_code ec;
                    p->resize(socket->available(ec));
                    if (ec) return done(e.first, SERVER_STATUS_ERROR, socket);
                    p->resize(socket->receive(buffer(*p), 0, ec));
                    if (ec) return done(e.first, SERVER_STATUS_ERROR, socket);
                    if (p->size() < 13 || p->read<query_type>() != SERVER_PONG) {
                        return done(e.first, SERVER_STATUS_VERSION_MISMATCH, socket);
                    }
                    auto server_version = p->read<uint32_t>();
                    if (PROTOCOL_VERSION < server_version) {
                        return done(e.first, SERVER_STATUS_OUTDATED_CLIENT, socket);
                    } else if (PROTOCOL_VERSION > server_version) {
                        return done(e.first, SERVER_STATUS_OUTDATED_SERVER, socket);
                    }
                    done(e.first, timestamp() - p->read<double>(), socket);
                });

                timer->async_wait([timer, socket](const asio::error_code& error) {
                    if (error) return;
                    socket->close();
                });
            });
        });
    }
}

void client::get_external_address() {
    auto s(weak_from_this());
    udp_resolver.async_resolve(ip::udp::v4(), "query.play64.com", "6400", [=](const auto& error, auto iterator) {
        if (s.expired()) return;
        auto socket = make_shared<ip::udp::socket>(service);
        socket->open(iterator->endpoint().protocol());
        socket->connect(*iterator);
        auto p(make_shared<packet>());
        *p << EXTERNAL_ADDRESS;
        socket->async_send(buffer(*p), [=](const error_code& error, size_t transferred) {
            if (s.expired()) return;
            p->reset();
            if (error) return;

            auto timer = make_shared<asio::steady_timer>(service);
            timer->expires_after(std::chrono::seconds(3));
            socket->async_wait(ip::udp::socket::wait_read, [=](const error_code& error) {
                if (s.expired()) return;
                timer->cancel();
                if (error) return;
                error_code ec;
                p->resize(socket->available(ec));
                if (ec) return;
                p->resize(socket->receive(buffer(*p), 0, ec));
                if (ec) return;
                if (p->read<query_type>() != EXTERNAL_ADDRESS) return;
                if (p->available() >= sizeof(uint16_t)) p->read<uint16_t>();
                if (p->available() == 4) {
                    std::array<uint8_t, 4> bytes;
                    for (auto& b : bytes) b = p->read<uint8_t>();
                    external_address = ip::make_address_v4(bytes);
                } else if (p->available() == 16) {
                    std::array<uint8_t, 16> bytes;
                    for (auto& b : bytes) b = p->read<uint8_t>();
                    external_address = ip::make_address_v6(bytes);
                }
            });

            timer->async_wait([timer, socket](const asio::error_code& error) {
                if (error) return;
                socket->close();
            });
        });
    });
}

string client::get_name() {
    return run([&] { return me->name; });
}

void client::set_name(const string& name) {
    run([&] {
        me->name = name;
        trim(me->name);
        my_dialog->info("Your name is " + name);
    });
}

void client::set_rom_info(const rom_info& rom) {
    run([&] {
        me->rom = rom;
        my_dialog->info("Your game is " + me->rom.to_string());
        if (rom.name == "MarioGolf64") {
            golf_mode_mask = MARIO_GOLF_MASK;
        } else {
            golf_mode_mask = 0xFFFFFFFF;
        }
    });
}

void client::set_src_controllers(CONTROL controllers[4]) {
    run([&] {
        for (int i = 0; i < 4; i++) {
            me->controllers[i].plugin = controllers[i].Plugin;
            me->controllers[i].present = controllers[i].Present;
            me->controllers[i].raw_data = controllers[i].RawData;
        }
        send_controllers();
    });
}

void client::set_dst_controllers(CONTROL controllers[4]) {
    run([&] { this->controllers = controllers; });
}

void client::process_input(array<BUTTONS, 4>& buttons) {
    run([&] {
#ifdef DEBUG
        //static uniform_int_distribution<uint32_t> dist(16, 63);
        //static random_device rd;
        //static uint32_t i = 0;
        //while (input_id >= i) i += dist(rd);
        //if (golf) buttons[0].A_BUTTON = (i & 1);
#endif
        input_data input = { buttons[0].Value, buttons[1].Value, buttons[2].Value, buttons[3].Value, me->map };
        repeated_input = (input == me->input ? repeated_input + 1 : 0);
        me->input = input;

        for (auto& u : user_list) {
            if (u->authority != me->id) continue;
            while (u->input_id <= input_id + u->lag) {
                send_input(*u);
            }
        }

        if (me->authority != me->id) {
            if (golf && input_detected(me->input, golf_mode_mask)) {
                me->pending = me->input;
                for (auto& u : user_list) {
                    change_input_authority(u->id, me->id);
                }
            } else if (udp_established) {
                if (repeated_input < INPUT_HISTORY_LENGTH || input_id % 30 == 0) {
                    send_input_update(me->input);
                }
            } else if (repeated_input == 0) {
                send_input_update(me->input);
            }
        }

        flush_all();
        
        on_input();
    });
    
    unique_lock<mutex> lock(next_input_mutex);
    next_input_condition.wait(lock, [=] { return !next_input.empty(); });
    buttons = next_input.front();
    next_input.pop_front();
}

void client::on_input() {
    for (auto& u : user_list) {
        if (u->input_queue.empty()) {
            return;
        }
    }

    unique_lock<mutex> lock(next_input_mutex);
    if (!next_input.empty()) {
        return;
    }

    array<BUTTONS, 4> result = { 0, 0, 0, 0 };
    array<int16_t, 4> analog_x = { 0, 0, 0, 0 };
    array<int16_t, 4> analog_y = { 0, 0, 0, 0 };

    for (auto& u : user_list) {
        if (u->input_queue.empty()) continue;
        auto input = u->input_queue.front();
        u->input_queue.pop_front();
        assert(u->input_id > input_id);

        auto b = input.map.bits;
        for (int i = 0; b && i < 4; i++) {
            BUTTONS buttons = { input.data[i] };
            for (int j = 0; b && j < 4; j++, b >>= 1) {
                if (b & 1) {
                    result[j].Value |= buttons.Value;
                    analog_x[j] += buttons.X_AXIS;
                    analog_y[j] += buttons.Y_AXIS;
                }
            }
        }
    }
    
    for (int i = 0; i < 4; i++) {
        double x = analog_x[i] + 0.5;
        double y = analog_y[i] + 0.5;
        double r = max(abs(x), abs(y));
        if (r > 127.5) {
            result[i].X_AXIS = (int)floor(x / r * 127.5);
            result[i].Y_AXIS = (int)floor(y / r * 127.5);
        } else {
            result[i].X_AXIS = analog_x[i];
            result[i].Y_AXIS = analog_y[i];
        }
    }

    next_input.push_back(result);
    next_input_condition.notify_one();

#ifdef DEBUG
    constexpr static char B[] = "><v^SZBA><v^RL";
    static array<BUTTONS, 4> prev = { 0, 0, 0, 0 };
    if (input_log.is_open() && result != prev) {
        input_log << setw(8) << setfill('0') << input_id << '|';
        for (int i = 0; i < 4; i++) {
            (result[i].X_AXIS ? input_log << setw(4) << setfill(' ') << showpos << result[i].X_AXIS << ' ' : input_log << "     ");
            (result[i].Y_AXIS ? input_log << setw(4) << setfill(' ') << showpos << result[i].Y_AXIS << ' ' : input_log << "     ");
            for (int j = static_cast<int>(strlen(B)) - 1; j >= 0; j--) {
                input_log << (result[i].Value & (1 << j) ? B[j] : ' ');
            }
            input_log << '|';
        }
        input_log << '\n';
    }
    prev = result;
#endif

    input_id++;

    input_times.push_back(timestamp());
    while (input_times.front() < input_times.back() - 2.0) {
        input_times.pop_front();
    }
}

void client::post_close() {
    service.post([&] { close(); });
}

client_dialog& client::get_dialog() {
    return *my_dialog;
}

bool client::wait_until_start() {
    if (started) return false;
    unique_lock<mutex> lock(start_mutex);
    start_condition.wait(lock, [=] { return started; });
    return true;
}

void client::on_message(string message) {
    try {
        if (message.substr(0, 1) == "/") {
            vector<string> params;
            for (auto start = message.begin(), end = start; start != message.end(); start = end) {
                start = find_if(start, message.end(), [](char ch) { return !isspace<char>(ch, locale::classic()); });
                end   = find_if(start, message.end(), [](char ch) { return  isspace<char>(ch, locale::classic()); });
                if (end > start) {
                    params.push_back(string(start, end));
                }
            }

            if (params[0] == "/name") {
                if (params.size() < 2) throw runtime_error("Missing parameter");

                me->name = params[1];
                trim(me->name);
                my_dialog->info("Your name is now " + me->name);
                send_name();
            } else if (params[0] == "/host" || params[0] == "/server") {
                if (started) throw runtime_error("Game has already started");

                host = "127.0.0.1";
                port = params.size() >= 2 ? stoi(params[1]) : 6400;
                path = "/";
                close();
                my_server = make_shared<server>(service, false);
                port = my_server->open(port);
                my_dialog->info("Server is listening on port " + to_string(port) + "...");
                connect(host, port, path);
            } else if (params[0] == "/join" || params[0] == "/connect") {
                if (started) throw runtime_error("Game has already started");
                if (params.size() < 2) throw runtime_error("Missing parameter");

                uri u(params[1]);
                if (!u.scheme.empty() && u.scheme != "play64") {
                    throw runtime_error("Unsupported protocol: " + u.scheme);
                }
                host = u.host;
                port = params.size() >= 3 ? stoi(params[2]) : (u.port ? u.port : 6400);
                path = u.path;
                close();
                connect(host, port, path);
            } else if (params[0] == "/start") {
                if (started) throw runtime_error("Game has already started");

                if (is_open()) {
                    send_start_game();
                } else {
                    map_src_to_dst();
                    set_lag(0);
                    start_game();
                }
            } else if (params[0] == "/lag") {
                if (params.size() < 2) throw runtime_error("Missing parameter");
                uint8_t lag = stoi(params[1]);
                if (!is_open()) throw runtime_error("Not connected");
                send_autolag(0);
                send_lag(lag, true, true);
                set_lag(lag);
            } else if (params[0] == "/my_lag") {
                if (params.size() < 2) throw runtime_error("Missing parameter");
                uint8_t lag = stoi(params[1]);
                send_lag(lag, true, false);
                set_lag(lag);
            } else if (params[0] == "/your_lag") {
                if (params.size() < 2) throw runtime_error("Missing parameter");
                if (!is_open()) throw runtime_error("Not connected");
                uint8_t lag = stoi(params[1]);
                send_autolag(0);
                send_lag(lag, false, true);
            } else if (params[0] == "/autolag") {
                if (!is_open()) throw runtime_error("Not connected");
                send_autolag();
            } else if (params[0] == "/golf") {
                if (!is_open()) throw runtime_error("Not connected");
                set_golf_mode(!golf);
                send(packet() << GOLF << golf);
                for (auto& u : user_list) {
                    change_input_authority(u->id, (golf ? me->id : u->id));
                }
            } else if (params[0] == "/map") {
                if (!is_open()) throw runtime_error("Not connected");

                input_map map;
                for (size_t i = 1; i < params.size(); i++) {
                    if (params[i].size() < 2 || params[i][1] != ':') throw runtime_error("Invalid controller map: \"" + params[i] + "\"");
                    int src = stoi(params[i].substr(0, 1)) - 1;
                    for (size_t j = 2; j < params[i].size(); j++) {
                        int dst = stoi(params[i].substr(j, 1)) - 1;
                        map.set(src, dst);
                    }
                }
                set_input_map(map);
            } else if (params[0] == "/auth") {
                if (!is_open()) throw runtime_error("Not connected");

                uint32_t authority_id = params.size() >= 2 ? stoi(params[1]) - 1 : me->id;
                uint32_t user_id      = params.size() >= 3 ? stoi(params[2]) - 1 : me->id;

                if (authority_id >= user_map.size() || !user_map[authority_id]) throw runtime_error("Invalid authority user ID");
                if (user_id      >= user_map.size() || !user_map[user_id])      throw runtime_error("Invalid user ID");

                if (user_map[user_id]->authority != authority_id) {
                    change_input_authority(user_id, authority_id);

                    if (user_id == authority_id) {
                        my_dialog->info("Input authority has been restored");
                        my_dialog->info("Please enable your frame rate limit.");
                    } else {
                        my_dialog->info("Input authority has been delegated to " + user_map[authority_id]->name);
                        my_dialog->info("Please disable your frame rate limit");
                    }
                }
            } else {
                throw runtime_error("Unknown command: " + params[0]);
            }
        } else {
            my_dialog->message(me->name, message);
            send_message(message);
        }
    } catch (const exception& e) {
        my_dialog->error(e.what());
    } catch (const error_code& e) {
        my_dialog->error(e.message());
    }
}

void client::set_lag(uint8_t lag) {
    me->lag = lag;
}

void client::set_golf_mode(bool golf) {
    if (golf == this->golf) return;
    this->golf = golf;
    if (golf) {
        my_dialog->info("Golf mode is enabled");
    } else {
        my_dialog->info("Golf mode is disabled");
    }
}

void client::remove_user(uint32_t user_id) {
    if (user_map.at(user_id) == me) return;

    if (started) {
        my_dialog->error(user_map.at(user_id)->name + " has quit");
    } else {
        my_dialog->info(user_map.at(user_id)->name + " has quit");
    }

    user_map.at(user_id) = nullptr;

    user_list.clear();
    for (auto& u : user_map) {
        if (u) user_list.push_back(u);
    }

    if (me->authority == user_id) {
        me->authority = me->id;
        send_delegate_authority(me->id, me->authority);

        if (started) {
            send_input(*me);
        }
    }

    update_user_list();

    if (started) {
        on_input();
    }
}

void client::message_received(uint32_t user_id, const string& message) {
    switch (user_id) {
        case ERROR_MSG:
            my_dialog->error(message);
            break;

        case INFO_MSG:
            my_dialog->info(message);
            break;

        default:
            my_dialog->message(user_map.at(user_id)->name, message);
            break;
    }
}

void client::close(const std::error_code& error) {
    connection::close(error);

    timer.cancel();

    if (my_server) {
        my_server->close();
        my_server.reset();
    }

    user_map.clear();
    user_list.clear();

    update_user_list();

    user_map.push_back(me);
    user_list.push_back(me);

    me->id = 0;
    me->authority = 0;
    me->lag = 0;
    me->latency = NAN;

    if (started) {
        send_input(*me);
        on_input();
    }
}

void client::start_game() {
    unique_lock<mutex> lock(start_mutex);
    if (started) return;
    started = true;
    start_condition.notify_all();
    my_dialog->info("Starting game...");
}

void client::connect(const string& host, uint16_t port, const string& room) {
    my_dialog->info("Connecting to " + host + (port == 6400 ? "" : ":" + to_string(port)) + "...");

    ip::tcp::resolver tcp_resolver(service);
    error_code error;
    auto endpoint = tcp_resolver.resolve(host, to_string(port), error);
    if (error) {
        return my_dialog->error(error.message());
    }

    if (!tcp_socket) {
        tcp_socket = make_shared<ip::tcp::socket>(service);
    }
    tcp_socket->connect(*endpoint, error);
    if (error) {
        return my_dialog->error(error.message());
    }
    tcp_socket->set_option(ip::tcp::no_delay(true), error);
    if (error) {
        return my_dialog->error(error.message());
    }

    try {
        if (!udp_socket) {
            udp_socket = make_shared<ip::udp::socket>(service);
        }
        auto udp_endpoint = ip::udp::endpoint(tcp_socket->local_endpoint().address(), 0);
        udp_socket->open(udp_endpoint.protocol());
        udp_socket->bind(udp_endpoint);
    } catch (error_code e) {
        if (udp_socket) {
            udp_socket->close(e);
            udp_socket.reset();
        }
    }

    my_dialog->info("Connected!");

    query_udp_port([=]() {
        send_join(room, external_udp_port);
    });

    receive_tcp_packet();
}

void client::on_receive(packet& p, bool udp) {
    switch (p.read<packet_type>()) {
        case VERSION: {
            auto protocol_version = p.read<uint32_t>();
            if (protocol_version != PROTOCOL_VERSION) {
                close();
                my_dialog->error("Server protocol version does not match client protocol version. Visit www.play64.com to get the latest version of the plugin.");
            }
            break;
        }

        case JOIN: {
            auto info = p.read<user_info>();
            my_dialog->info(info.name + " has joined");
            auto u = make_shared<user_info>(info);
            user_map.push_back(u);
            user_list.push_back(u);
            update_user_list();
            break;
        }

        case ACCEPT: {
            auto udp_port = p.read<uint16_t>();
            if (udp_socket && udp_port) {
                udp_socket->connect(ip::udp::endpoint(tcp_socket->remote_endpoint().address(), udp_port));
                receive_udp_packet();
            } else {
                udp_socket.reset();
            }
            on_tick();

            user_map.clear();
            user_list.clear();
            while (p.available()) {
                if (p.read<bool>()) {
                    auto u = make_shared<user_info>(p.read<user_info>());
                    user_map.push_back(u);
                    user_list.push_back(u);
                } else {
                    user_map.push_back(nullptr);
                }
            }
            me = user_list.back();
            break;
        }

        case PATH: {
            path = p.read<string>();
            my_dialog->info(
                "Others may join with the following command:\r\n\r\n"
                "/join " + (host == "127.0.0.1" ? (external_address.is_unspecified() ? "<Your IP>" : external_address.to_string()) : host) + (port == 6400 ? "" : ":" + to_string(port)) + (path == "/" ? "" : path) + "\r\n"
            );
            break;
        }

        case PING: {
            packet pong;
            pong << PONG;
            while (p.available()) {
                pong << p.read<uint8_t>();
            }
            if (udp) {
                send_udp(pong);
            } else {
                send(pong);
            }
            break;
        }

        case PONG: {
            if (udp && !udp_established) {
                udp_established = true;
                tcp_socket->set_option(ip::tcp::no_delay(false));
            }
            break;
        }

        case QUIT: {
            remove_user(p.read<uint32_t>());
            break;
        }

        case NAME: {
            auto user = user_map.at(p.read<uint32_t>());
            auto name = p.read<string>();
            my_dialog->info(user->name + " is now " + name);
            user->name = name;
            update_user_list();
            break;
        }

        case LATENCY: {
            for (auto& u : user_list) {
                u->latency = p.read<double>();
            }
            update_user_list();
            break;
        }

        case MESSAGE: {
            auto user_id = p.read<uint32_t>();
            auto message = p.read<string>();
            message_received(user_id, message);
            break;
        }

        case LAG: {
            auto lag = p.read<uint8_t>();
            auto source_id = p.read<uint32_t>();
            while (p.available()) {
                user_map.at(p.read<uint32_t>())->lag = lag;
            }
            update_user_list();
            break;
        }

        case CONTROLLERS: {
            for (auto& u : user_list) {
                for (auto& c : u->controllers) {
                    p >> c;
                }
                p >> u->map;
            }

            if (!started) {
                for (int j = 0; j < 4; j++) {
                    controllers[j].Present = 1;
                    controllers[j].RawData = 0;
                    controllers[j].Plugin = PLUGIN_NONE;
                    for (int i = 0; i < 4; i++) {
                        for (auto& u : user_list) {
                            if (u->map.get(i, j)) {
                                controllers[j].Plugin = max(controllers[j].Plugin, u->controllers[i].plugin);
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

        case GOLF: {
            set_golf_mode(p.read<bool>());
            break;
        }

        case INPUT_MAP: {
            auto user = user_map.at(p.read<uint32_t>());
            if (!user) break;
            user->map = p.read<input_map>();
            update_user_list();
            break;
        }

        case INPUT_DATA: {
            while (p.available()) {
                auto user = user_map.at(p.read_var<uint32_t>());
                if (!user) continue;
                auto input_id = p.read_var<uint32_t>();
                packet pin;
                pin.transpose(p.read_rle(), input_data::SIZE);
                while (pin.available()) {
                    auto input = pin.read<input_data>();
                    if (!user->add_input_history(input_id++, input)) continue;
                    user->input_queue.push_back(input);
                    if (golf && me->authority == me->id && input_detected(input, golf_mode_mask)) {
                        change_input_authority(me->id, user->id);
                    }
                }
            }
            on_input();
            break;
        }

        case INPUT_UPDATE: {
            auto user = user_map.at(p.read<uint32_t>());
            if (!user) break;
            user->input = p.read<input_data>();
            break;
        }

        case REQUEST_AUTHORITY: {
            auto user = user_map.at(p.read<uint32_t>());
            auto authority = user_map.at(p.read<uint32_t>());
            if (!user || !authority) break;
            if (user->authority == me->id) {
                change_input_authority(user->id, authority->id);
            }
            break;
        }

        case DELEGATE_AUTHORITY: {
            auto user = user_map.at(p.read<uint32_t>());
            auto authority = user_map.at(p.read<uint32_t>());
            if (!user || !authority) break;
            user->authority = authority->id;
            if (user->authority == me->id) {
                user->input = user->pending;
                user->pending = input_data();
                send_input(*user);
                send_input(*user);
                on_input();
            }
            update_user_list();
            break;
        }
    }
}

void client::map_src_to_dst() {
    me->map = input_map::IDENTITY_MAP;
    for (int i = 0; i < 4; i++) {
        controllers[i].Plugin = me->controllers[i].plugin;
        controllers[i].Present = me->controllers[i].present;
        controllers[i].RawData = me->controllers[i].raw_data;
    }
}

void client::update_user_list() {
    vector<vector<string>> lines;
    lines.reserve(user_list.size());

    for (auto& u : user_list) {
        vector<string> line;

        line.push_back(to_string(u->id + 1));


        line.push_back(u->id == u->authority ? "" : to_string(u->authority + 1));

        line.push_back(u->name);

        for (int j = 0; j < 4; j++) {
            string m;
            for (int i = 0; i < 4; i++) {
                if (u->map.get(i, j)) {
                    if (!m.empty()) m += ",";
                    m += to_string(i + 1);
                    switch (u->controllers[i].plugin) {
                        case PLUGIN_MEMPAK: m += 'M'; break;
                        case PLUGIN_RUMBLE_PAK: m += 'R'; break;
                        case PLUGIN_TANSFER_PAK: m += 'T'; break;
                    }
                }
            }
            line.push_back(m);
        }


        line.push_back(to_string(u->lag));

        if (!isnan(u->latency)) {
            line.push_back(to_string((int)(u->latency * 1000)) + " ms");
        } else {
            line.push_back("");
        }

        lines.push_back(line);
    }

    my_dialog->update_user_list(lines);
}

void client::change_input_authority(uint32_t user_id, uint32_t authority_id) {
    auto user = user_map.at(user_id);
    if (user->authority == authority_id) return;

    if (user->authority == me->id) {
        user->authority = authority_id;
        send_delegate_authority(user->id, authority_id);
    } else {
        send_request_authority(user->id, authority_id);
    }
}

void client::set_input_map(input_map new_map) {
    if (me->map == new_map) return;

    me->map = new_map;
    send_input_map(new_map);

    update_user_list();
}

void client::on_tick() {
    if (!input_times.empty()) {
        send_input_rate((input_times.size() - 1) / (float)(input_times.back() - input_times.front()));
    }

    if (!udp_established) {
        send_udp_ping();
    }

    timer.expires_after(500ms);
    auto self(weak_from_this());
    timer.async_wait([self, this](const error_code& error) { 
        if (self.expired()) return;
        if (!error) on_tick();
    });
}

void client::send_join(const string& room, uint16_t udp_port) {
    send(packet() << JOIN << PROTOCOL_VERSION << room << *me << udp_port);
}

void client::send_name() {
    send(packet() << NAME << me->name);
}

void client::send_message(const string& message) {
    send(packet() << MESSAGE << message);
}

void client::send_controllers() {
    packet p;
    p << CONTROLLERS;
    for (auto& c : me->controllers) {
        p << c;
    }
    send(p);
}

void client::send_start_game() {
    send(packet() << START);
}

void client::send_lag(uint8_t lag, bool my_lag, bool your_lag) {
    send(packet() << LAG << lag << my_lag << your_lag);
}

void client::send_autolag(int8_t value) {
    send(packet() << AUTOLAG << value);
}

void client::send_input(user_info& user) {
    user.add_input_history(user.input_id, user.input);
    user.input_queue.push_back(user.input);

    if (!is_open()) return;

    if (udp_established) {
        packet p;
        p << INPUT_DATA;
        p.write_var(user.id);
        p.write_var(user.input_id - user.input_history.size());
        p.write_rle(packet() << user.input_history);
        send_udp(p, false);
    }

    packet p;
    p << INPUT_DATA;
    p.write_var(user.id);
    p.write_var(user.input_id - 1);
    p.write_rle(packet() << user.input_history.back());
    send(p, false);
}

void client::send_input_update(const input_data& input) {
    if (udp_established) {
        send_udp(packet() << INPUT_UPDATE << input);
    } else {
        send(packet() << INPUT_UPDATE << input);
    }
}

void client::send_input_map(input_map map) {
    send(packet() << INPUT_MAP << map);
}

void client::send_input_rate(float rate) {
    send(packet() << INPUT_RATE << rate);
}

void client::send_udp_ping() {
    send_udp(packet() << PING << timestamp());
}

void client::send_request_authority(uint32_t user_id, uint32_t authority_id) {
    send(packet() << REQUEST_AUTHORITY << user_id << authority_id);
}

void client::send_delegate_authority(uint32_t user_id, uint32_t authority_id) {
    send(packet() << DELEGATE_AUTHORITY << user_id << authority_id);
}
