#include <boost/bind.hpp>
#include <boost/lexical_cast.hpp>

#include "server.h"
#include "session.h"
#include "client_dialog.h"
#include "client_server_common.h"
#include "game.h"

using namespace boost::asio;
using namespace std;

server::server(client_dialog& my_dialog, uint8_t lag) : my_dialog(my_dialog), work(io_s), acceptor(io_s), thread(boost::bind(&io_service::run, &io_s)) {
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

    for (map<uint32_t, session_ptr>::iterator it = sessions.begin(); it != sessions.end(); ++it) {
        it->second->stop();
    }

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

    begin_accept();

    return acceptor.local_endpoint().port();
}

void server::begin_accept() {
    session_ptr s = session_ptr(new session(*this, next_id++));
    acceptor.async_accept(s->socket, boost::bind(&server::accepted, this, s, boost::asio::placeholders::error));
}

void server::accepted(session_ptr s, const boost::system::error_code& error) {
    if (error) {
        return;
    }

    boost::system::error_code ec;
    s->socket.set_option(ip::tcp::no_delay(true), ec);
    if (ec) {
        return;
    }

    s->send_version();
    s->send_lag(lag);
    s->read_command();

    for (map<uint32_t, session_ptr>::iterator it = sessions.begin(); it != sessions.end(); ++it) {
        s->send_name(it->first, it->second->get_name());
    }

    sessions[s->get_id()] = s;

    begin_accept();
}

void server::remove_session(uint32_t id) {
    if (sessions.find(id) == sessions.end()) {
        return;
    }

    for (map<uint32_t, session_ptr>::iterator it = sessions.begin(); it != sessions.end(); ++it) {
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
    for (map<uint32_t, session_ptr>::iterator it = sessions.begin(); it != sessions.end(); ++it) {
        uint8_t player_start = (uint8_t) all_controllers.size();
        uint8_t player_count = 0;

        const vector<CONTROL>& controllers = it->second->get_controllers();
        for (int i = 0; i < controllers.size() && all_controllers.size() < MAX_PLAYERS; i++) {
            if (controllers[i].Present) {
                all_controllers.push_back(controllers[i]);
                player_count++;
            }
        }

        it->second->send_controller_range(player_start, player_count);
    }

    all_controllers.resize(MAX_PLAYERS);
    for (map<uint32_t, session_ptr>::iterator it = sessions.begin(); it != sessions.end(); ++it) {
        it->second->send_controllers(all_controllers);
        it->second->send_start_game();
    }
}

void server::send_input(uint32_t id, uint8_t start, const vector<BUTTONS> buttons) {
    for (map<uint32_t, session_ptr>::iterator it = sessions.begin(); it != sessions.end(); ++it) {
        if (it->first != id) {
            it->second->send_input(start, buttons);
        }
    }
}

void server::send_name(uint32_t id, const vector<wchar_t>& name) {
    for (map<uint32_t, session_ptr>::iterator it = sessions.begin(); it != sessions.end(); ++it) {
        it->second->send_name(id, name);
    }
}

void server::send_message(uint32_t id, const vector<wchar_t>& message) {
    for (map<uint32_t, session_ptr>::iterator it = sessions.begin(); it != sessions.end(); ++it) {
        if (it->first != id) {
            it->second->send_message(id, message);
        }
    }
}

void server::send_lag(uint32_t id, uint8_t lag) {
    this->lag = lag;

    for (map<uint32_t, session_ptr>::iterator it = sessions.begin(); it != sessions.end(); ++it) {
        if (it->first != id) {
            it->second->send_lag(lag);
        }
    }
}
