#include <cmath>
#include <boost/bind.hpp>
#include <boost/lexical_cast.hpp>

#include "server.h"
#include "session.h"
#include "client_dialog.h"
#include "client_server_common.h"
#include "game.h"

using namespace boost::asio;
using namespace std;

server::server(client_dialog& my_dialog, uint8_t lag) : my_dialog(my_dialog), work(io_s), acceptor(io_s), timer(io_s), thread(boost::bind(&io_service::run, &io_s)) {
    next_id = 0;
    game_started = false;
    this->lag = lag;
}

server::~server() {
    io_s.post(boost::bind(&server::stop, this));

    thread.join();
}

void server::stop() {
    boost::system::error_code error;
    if (acceptor.is_open()) {
        acceptor.close(error);
    }

    for (auto it = sessions.begin(); it != sessions.end(); ++it) {
        it->second->stop();
    }

    timer.cancel();

    io_s.stop();
}

// TODO: MESSAGE WHEN THERE IS AN ERROR
uint16_t server::start(uint16_t port) {
    boost::system::error_code error;

    acceptor.open(ip::tcp::v6(), error);
    if (error) {
        acceptor.open(ip::tcp::v4(), error);
        if (error) {
            return 0;
        }

        acceptor.bind(boost::asio::ip::tcp::endpoint(ip::tcp::v4(), port), error);
        if (error) {
            return 0;
        }
    } else {
        acceptor.bind(boost::asio::ip::tcp::endpoint(ip::tcp::v6(), port), error);
    }

    if (error) {
        return 0;
    }

    acceptor.listen(MAX_PLAYERS, error);
    if (error) {
        return 0;
    }

    my_dialog.status(L"Server is listening on port " + boost::lexical_cast<wstring>(acceptor.local_endpoint().port()) + L"...");

    timer.expires_from_now(std::chrono::seconds(1));
    timer.async_wait(boost::bind(&server::on_tick, this, boost::asio::placeholders::error));

    accept();

    return acceptor.local_endpoint().port();
}

void server::accept() {
    session_ptr s = session_ptr(new session(*this, next_id++));

    acceptor.async_accept(s->socket, [=](auto& error) {
        if (error) return;

        boost::system::error_code ec;
        s->socket.set_option(ip::tcp::no_delay(true), ec);
        if (ec) return;

        s->send_protocol_version();
        s->send_lag(lag);
        s->send_ping();
        for (auto it = sessions.begin(); it != sessions.end(); ++it) {
            s->send_name(it->first, it->second->get_name());
        }

        s->read_command();

        sessions[s->get_id()] = s;

        accept();
    });
}

int32_t server::get_total_latency() {
    int32_t max_latency = -1, second_max_latency = -1;
    for (auto it = sessions.begin(); it != sessions.end(); ++it) {
        if (it->second->is_player()) {
            auto latency = it->second->get_average_latency();
            if (latency > second_max_latency) {
                second_max_latency = latency;
            }
            if (second_max_latency > max_latency) {
                std::swap(second_max_latency, max_latency);
            }
        }
    }
    return second_max_latency >= 0 ? max_latency + second_max_latency : 0;
}

void server::on_tick(const boost::system::error_code& error) {
    if (error) {
        return;
    }

    send_latencies();

    for (auto it = sessions.begin(); it != sessions.end(); ++it) {
        it->second->send_ping();
    }

    if (auto_lag) {
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
    timer.async_wait(boost::bind(&server::on_tick, this, boost::asio::placeholders::error));
}

void server::remove_session(uint32_t id) {
    if (sessions.find(id) == sessions.end()) {
        return;
    }

    for (auto it = sessions.begin(); it != sessions.end(); ++it) {
        it->second->send_departure(id);
    }

    if (sessions[id]->is_player()) {
        stop();
    } else {
        sessions.erase(id);
    }
}

void server::send_start_game() {
    if (game_started) {
        return;
    }

    game_started = true;

    boost::system::error_code error;
    if (acceptor.is_open()) {
        acceptor.close(error);
    }

    vector<CONTROL> all_controllers;
    for (auto it = sessions.begin(); it != sessions.end(); ++it) {
        uint8_t player_index = (uint8_t) all_controllers.size();
        uint8_t player_count = 0;

        const vector<CONTROL>& controllers = it->second->get_controllers();
        for (int i = 0; i < controllers.size() && all_controllers.size() < MAX_PLAYERS; i++) {
            if (controllers[i].Present) {
                all_controllers.push_back(controllers[i]);
                player_count++;
            }
        }

        it->second->send_controller_range(player_index, player_count);
    }

    all_controllers.resize(MAX_PLAYERS);
    for (auto it = sessions.begin(); it != sessions.end(); ++it) {
        it->second->send_controllers(all_controllers);
        it->second->send_start_game();
    }
}

void server::send_input(uint32_t id, uint8_t start, uint32_t frame, const vector<BUTTONS> buttons) {
    for (auto it = sessions.begin(); it != sessions.end(); ++it) {
        if (it->first != id) {
            it->second->send_input(start, buttons);
        }
    }
}

void server::send_name(uint32_t id, const wstring& name) {
    for (auto it = sessions.begin(); it != sessions.end(); ++it) {
        it->second->send_name(id, name);
    }
}

void server::send_message(int32_t id, const wstring& message) {
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
            it->second->send_message(-1, (id == -1 ? L"(SERVER)" : sessions[id]->get_name()) + L" set the lag to " + boost::lexical_cast<wstring>((int)lag));
        }
    }
}

void server::send_latencies() {
    packet p;
    p << LATENCIES;
    p << (uint32_t)sessions.size();
    for (auto it = sessions.begin(); it != sessions.end(); ++it) {
        p << it->first << it->second->get_latency();
    }
    for (auto it = sessions.begin(); it != sessions.end(); ++it) {
        it->second->send(p);
    }
}