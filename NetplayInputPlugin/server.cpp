#include "stdafx.h"

#include "server.h"
#include "session.h"
#include "client_server_common.h"

using namespace std;
using namespace asio;

server::server(io_service& io_s, uint8_t lag) : io_s(io_s), acceptor(io_s), timer(io_s), start_time(std::chrono::high_resolution_clock::now()) {
    next_id = 0;
    game_started = false;
    this->lag = lag;
}

server::~server() {
    
}

void server::stop() {
    error_code error;

    if (acceptor.is_open()) {
        acceptor.close(error);
    }

    timer.cancel(error);

    for (auto it = sessions.begin(); it != sessions.end(); ++it) {
        it->second->stop();
    }
}

// TODO: MESSAGE WHEN THERE IS AN ERROR
uint16_t server::start(uint16_t port) {
    error_code error;

    acceptor.open(ip::tcp::v6(), error);
    if (error) {
        acceptor.open(ip::tcp::v4(), error);
        if (error) {
            return 0;
        }

        acceptor.bind(ip::tcp::endpoint(ip::tcp::v4(), port), error);
        if (error) {
            return 0;
        }
    } else {
        acceptor.bind(ip::tcp::endpoint(ip::tcp::v6(), port), error);
    }

    if (error) {
        throw error;
    }

    acceptor.listen(MAX_PLAYERS, error);
    if (error) {
        throw error;
    }

    timer.expires_from_now(std::chrono::seconds(1));
    timer.async_wait([=] (const error_code& error) { on_tick(error); });

    accept();

    return acceptor.local_endpoint().port();
}   

uint64_t server::time() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::high_resolution_clock::now() - start_time).count();
}

int server::player_count() {
    int count = 0;
    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (netplay_controllers[i].Present) {
            count++;
        }
    }
    return count;
}

void server::accept() {
    session_ptr s = session_ptr(new session(shared_from_this(), next_id++));

    acceptor.async_accept(s->socket, [=](const error_code& error) {
        if (error) return;

        accept();

        error_code ec;
        s->socket.set_option(ip::tcp::no_delay(true), ec);
        if (ec) return;

        s->send_protocol_version();
        s->process_packet();
    });
}

void server::on_session_joined(session_ptr s) {
    for (auto it = sessions.begin(); it != sessions.end(); ++it) {
        it->second->send_join(s->get_id(), s->get_name());
    }
    sessions[s->get_id()] = s;
    for (auto it = sessions.begin(); it != sessions.end(); ++it) {
        s->send_join(it->second->get_id(), it->second->get_name());
    }
    s->send_lag(lag);
    s->send_ping(time());
    
    update_controllers();
}

int32_t server::get_total_latency() {
    int32_t max_latency = -1, second_max_latency = -1;
    for (auto it = sessions.begin(); it != sessions.end(); ++it) {
        if (it->second->is_player()) {
            auto latency = it->second->get_minimum_latency();
            if (latency > second_max_latency) {
                second_max_latency = latency;
            }
            if (second_max_latency > max_latency) {
                swap(second_max_latency, max_latency);
            }
        }
    }
    return second_max_latency >= 0 ? max_latency + second_max_latency : 0;
}

void server::on_tick(const error_code& error) {
    if (error) {
        return;
    }

    send_latencies();

    for (auto it = sessions.begin(); it != sessions.end(); ++it) {
        it->second->send_ping(time());
    }

    if (autolag) {
        uint32_t fps = 0;
        for (auto it = sessions.begin(); it != sessions.end(); ++it) {
            if (it->second->is_player()) {
                fps = it->second->get_fps();
                break;
            }
        }
        if (fps > 0) {
            int ideal_lag = min((int)ceil(get_total_latency() * fps / 1000.0), 255);
            if (ideal_lag < lag) {
                send_lag(-1, lag - 1);
            } else if (ideal_lag > lag) {
                send_lag(-1, lag + 1);
            }
        }
    }

    timer.expires_at(timer.expiry() + std::chrono::seconds(1));
    timer.async_wait([=](const error_code& error) { on_tick(error); });
}

void server::on_session_quit(session_ptr session) {
    if (sessions.find(session->get_id()) == sessions.end()) {
        return;
    }

    for (auto it = sessions.begin(); it != sessions.end(); ++it) {
        it->second->send_quit(session->get_id());
    }

    if (sessions[session->get_id()]->is_player()) {
        stop();
    } else {
        sessions.erase(session->get_id());
    }
}

void server::send_start_game() {
    if (game_started) {
        return;
    }

    game_started = true;

    error_code error;
    if (acceptor.is_open()) {
        acceptor.close(error);
    }

    for (auto it = sessions.begin(); it != sessions.end(); ++it) {
        it->second->send_start_game();
    }
}

void server::send_input(uint32_t id, uint8_t port, controller::BUTTONS buttons) {
    for (auto it = sessions.begin(); it != sessions.end(); ++it) {
        if (it->first != id) {
            it->second->send_input(port, buttons);
        }
    }
}

void server::send_name(uint32_t id, const string& name) {
    for (auto it = sessions.begin(); it != sessions.end(); ++it) {
        it->second->send_name(id, name);
    }
}

void server::update_controllers() {
    netplay_controllers.fill(controller::CONTROL());
    uint8_t netplay_port = 0;
    for (auto it = sessions.begin(); it != sessions.end(); ++it) {
        const auto& local_controllers = it->second->get_controllers();
        for (uint8_t local_port = 0; local_port < local_controllers.size(); local_port++) {
            if (local_controllers[local_port].Present && netplay_port < netplay_controllers.size()) {
                netplay_controllers[netplay_port] = local_controllers[local_port];
                it->second->my_controller_map.insert(local_port, netplay_port);
                netplay_port++;
            } else {
                it->second->my_controller_map.insert(local_port, -1);
            }
        }
    }

    for (auto it = sessions.begin(); it != sessions.end(); ++it) {
        it->second->send_netplay_controllers(netplay_controllers);

        packet p;
        p << CONTROLLERS;
        p << it->first;
        for (auto& c : it->second->controllers) {
            p << c.Plugin << c.Present << c.RawData;
        }
        for (auto l2n : it->second->my_controller_map.local_to_netplay) {
            p << l2n;
        }

        for (auto jt = sessions.begin(); jt != sessions.end(); ++jt) {
            jt->second->send(p);
        }
    }
}


void server::send_message(int32_t id, const string& message) {
    for (auto it = sessions.begin(); it != sessions.end(); ++it) {
        if (it->first != id) {
            it->second->send_message(id, message);
        }
    }
}

void server::send_lag(int32_t id, uint8_t lag) {
    this->lag = lag;

    for (auto it = sessions.begin(); it != sessions.end(); ++it) {
        if (it->first != id) {
            it->second->send_lag(lag);
            it->second->send_message(-1, (id == -1 ? "(Server)" : sessions[id]->get_name()) + " set the lag to " + to_string((int)lag));
        }
    }
}

void server::send_latencies() {
    packet p;
    p << LATENCY;
    for (auto it = sessions.begin(); it != sessions.end(); ++it) {
        p << it->first << it->second->get_latency();
    }
    for (auto it = sessions.begin(); it != sessions.end(); ++it) {
        it->second->send(p);
    }
}

int main(int argc, char* argv[]) {
    if (argc != 2) {
        cerr << "Usage: " << argv[0] << " <port>" << endl;
        return 1;
    }
    
    try {
        uint16_t port = stoi(argv[1]);

        io_service io_s;
        auto my_server = make_shared<server>(io_s, 5);
        port = my_server->start(port);

        cout << "Listening on port " << port << "..." << endl;
        io_s.run();
    } catch (const exception& e) {
        cerr << e.what() << endl;
        return 1;
    } catch (const error_code& e) {
        cerr << e.message() << endl;
        return 1;
    }

    return 0;
}
