#include <boost/bind.hpp>

#include "server.h"
#include "session.h"
#include "client_server_common.h"

using namespace std;
using namespace boost::asio;

session::session(server& my_server, uint32_t id) : socket(my_server.io_s), my_server(my_server), id(id), controllers(MAX_PLAYERS) { }

void session::handle_error(const boost::system::error_code& error) {
    if (error == error::operation_aborted) {
        return;
    }

    my_server.remove_session(id);
}

void session::stop() {
    boost::system::error_code error;
    socket.shutdown(boost::asio::ip::tcp::socket::shutdown_both, error);
    socket.close(error);
}

uint32_t session::get_id() const {
    return id;
}

bool session::is_player() {
    return in_buttons.size() > 0;
}

const vector<CONTROL>& session::get_controllers() const {
    return controllers;
}

const vector<wchar_t>& session::get_name() const {
    return name;
}

void session::read_command() {
    async_read(socket, buffer(&one_byte, sizeof(one_byte)), boost::bind(&session::command_read, shared_from_this(), boost::asio::placeholders::error));
}

void session::send_controller_range(uint8_t player_start, uint8_t player_count) {
    this->player_start = player_start;
    in_buttons.resize(player_count);

    send(packet() << PLAYER_RANGE << player_start << player_count);
}

void session::send_controllers(const vector<CONTROL>& controllers) {
    assert(controllers.size() == MAX_PLAYERS);

    int total_count = 0;
    for (int i = 0; i < controllers.size(); i++) {
        if (controllers[i].Present) {
            total_count++;
        }
    }

    out_buttons.resize(total_count);

    send(packet() << CONTROLLERS << controllers);
}

void session::send_start_game() {
    send(packet() << START_GAME);
}

void session::send_name(uint32_t id, const vector<wchar_t>& name) {
    packet p;
    p << NAME;
    p << id;
    p << (uint8_t) name.size();
    p << name;

    send(p);
}

void session::send_departure(uint32_t id) {
    send(packet() << LEFT << id);
}

void session::send_message(uint32_t id, const vector<wchar_t>& message) {
    packet p;
    p << CHAT;
    p << id;
    p << (uint16_t) message.size();
    p << message;

    send(p);
}

void session::command_read(const boost::system::error_code& error) {
    if (error) {
        handle_error(error);
        return;
    }

    switch (one_byte) {
        case VERSION:
            async_read(socket, buffer(&two_bytes, sizeof(two_bytes)), boost::bind(&session::client_version_read, shared_from_this(), boost::asio::placeholders::error));
            break;

        case KEEP_ALIVE:
            read_command();
            break;

        case CONTROLLERS:
            async_read(socket, buffer(controllers), boost::bind(&session::controllers_read, shared_from_this(), boost::asio::placeholders::error));
            break;

        case NAME:
            async_read(socket, buffer(&one_byte, sizeof(one_byte)), boost::bind(&session::name_length_read, shared_from_this(), boost::asio::placeholders::error));
            break;

        case CHAT:
            async_read(socket, buffer(&two_bytes, sizeof(two_bytes)), boost::bind(&session::message_length_read, shared_from_this(), boost::asio::placeholders::error));
            break;

        case LAG:
            async_read(socket, buffer(&one_byte, sizeof(one_byte)), boost::bind(&session::lag_read, shared_from_this(), boost::asio::placeholders::error));
            break;

        case START_GAME:
            my_server.send_start_game();
            read_command();
            break;

        case INPUT_DATA:
            async_read(socket, buffer(in_buttons), boost::bind(&session::input_read, shared_from_this(), boost::asio::placeholders::error));
            break;
    }
}

void session::input_read(const boost::system::error_code& error) {
    if (error) {
        handle_error(error);
        return;
    }

    my_server.send_input(id, player_start, in_buttons);

    read_command();
}

void session::client_version_read(const boost::system::error_code& error) {
    if (error) {
        handle_error(error);
        return;
    }

    read_command();
}

void session::name_length_read(const boost::system::error_code& error) {
    if (error) {
        handle_error(error);
        return;
    }

    name.resize(one_byte);
    async_read(socket, buffer(name), boost::bind(&session::name_read, shared_from_this(), boost::asio::placeholders::error));
}

void session::name_read(const boost::system::error_code& error) {
    if (error) {
        handle_error(error);
        return;
    }

    my_server.send_name(id, name);

    read_command();
}

void session::controllers_read(const boost::system::error_code& error) {
    if (error) {
        handle_error(error);
        return;
    }

    read_command();
}

void session::message_length_read(const boost::system::error_code& error) {
    if (error) {
        handle_error(error);
        return;
    }

    text.resize(two_bytes);
    async_read(socket, buffer(text), boost::bind(&session::message_read, shared_from_this(), boost::asio::placeholders::error));
}

void session::message_read(const boost::system::error_code& error) {
    if (error) {
        handle_error(error);
        return;
    }

    my_server.send_message(id, text);

    read_command();
}

void session::lag_read(const boost::system::error_code& error) {
    if (error) {
        handle_error(error);
        return;
    }

    my_server.send_lag(id, one_byte);

    read_command();
}

void session::send_lag(uint8_t lag) {
    send(packet() << LAG << lag);
}

void session::send_version() {
    send(packet() << VERSION << MY_VERSION);
}

void session::send(const packet& p) {
    out_buffer.push_back(p);

    if (output.empty()) {
        begin_send();
    }
}

void session::begin_send() {
    while (!out_buffer.empty()) {
        output << out_buffer.front();
        out_buffer.pop_front();
    }

    async_write(socket, buffer(output.data()), boost::bind(&session::data_sent, shared_from_this(), boost::asio::placeholders::error));
}

void session::data_sent(const boost::system::error_code& error) {
    output.clear();

    if (error) {
        handle_error(error);
        return;
    }

    if (!out_buffer.empty()) {
        begin_send();
    }
}

void session::send_input(uint8_t player_start, const vector<BUTTONS>& input) {
    for (int i = 0; i < input.size(); i++) {
        out_buttons[player_start + i].push_back(input[i]);
    }

    if (ready_to_send_input()) {
        send_input();
    }
}

bool session::is_me(uint8_t player) {
    return player_start <= player && player < player_start + in_buttons.size();
}

bool session::ready_to_send_input() {
    for (int i = 0; i < out_buttons.size(); i++) {
        if (!is_me(i) && out_buttons[i].empty()) {
            return false;
        }
    }

    return true;
}

void session::send_input() {
    packet p;
    p << INPUT_DATA;
    for (int i = 0; i < out_buttons.size(); i++) {
        if (!is_me(i)) {
            p << out_buttons[i].front();
            out_buttons[i].pop_front();
        }
    }

    send(p);
}
