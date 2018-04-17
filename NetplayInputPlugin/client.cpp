#include <boost/lexical_cast.hpp>
#include <string>

#include "client.h"
#include "game.h"
#include "client_dialog.h"
#include "util.h"

using namespace std;
using namespace boost::asio;

client::client(client_dialog& my_dialog, game& my_game)
  : my_dialog(my_dialog), my_game(my_game), work(io_s), resolver(io_s), socket(io_s), thread(boost::bind(&io_service::run, &io_s)) {
    is_connected = false;
    incoming_controls.resize(MAX_PLAYERS);
}

client::~client() {
    io_s.post(boost::bind(&client::stop, this));

    thread.join();
}

void client::stop() {
    resolver.cancel();

    boost::system::error_code error;
    socket.shutdown(boost::asio::ip::tcp::socket::shutdown_both, error);
    socket.close(error);

    out_buffer.clear();
    output.clear();

    is_connected = false;

    io_s.stop();
}

void client::handle_error(const boost::system::error_code& error, bool lost_connection) {
    if (error == error::operation_aborted) {
        return;
    }

    if (lost_connection) {
        stop();
        my_game.client_error();
    }

    my_dialog.error(L"\"" + widen(error.message()) + L"\"");
}

void client::connect(const wstring& host, uint16_t port) {
    my_dialog.status(L"Resolving...");
    resolver.async_resolve(ip::tcp::resolver::query(narrow(host), boost::lexical_cast<string>(port)),
                           boost::bind(&client::resolved, this, boost::asio::placeholders::error, boost::asio::placeholders::iterator));
}

void client::resolved(const boost::system::error_code& error, ip::tcp::resolver::iterator iterator) {
    if (error) {
        handle_error(error, false);
        return;
    }

    my_dialog.status(L"Resolved!");

    begin_connect(iterator);
}

void client::begin_connect(ip::tcp::resolver::iterator iterator) {
    my_dialog.status(L"Connecting to server...");

    ip::tcp::endpoint endpoint = *iterator;
    socket.async_connect(endpoint, boost::bind(&client::connected, this, boost::asio::placeholders::error, ++iterator));
}

void client::connected(const boost::system::error_code& error, ip::tcp::resolver::iterator iterator) {
    if (error) {
        if (iterator != ip::tcp::resolver::iterator()) {
            socket.close();
            begin_connect(iterator);
        } else {
            handle_error(error, false);
        }

        return;
    }

    boost::system::error_code ec;
    socket.set_option(ip::tcp::no_delay(true), ec);
    if (ec) {
        handle_error(ec, false);
        return;
    }

    is_connected = true;

    read_command();

    send_version();
    send_name(my_game.get_name());
    send_controllers(my_game.get_local_controllers());

    my_dialog.status(L"Connected!");
}

void client::read_command() {
    async_read(socket, buffer(&one_byte, sizeof(one_byte)), boost::bind(&client::command_read, this, boost::asio::placeholders::error));
}

void client::command_read(const boost::system::error_code& error) {
    if (error) {
        handle_error(error, true);
        return;
    }

    switch (one_byte) {
        case VERSION:
            async_read(socket, buffer(&two_bytes, sizeof(two_bytes)), boost::bind(&client::server_version_read, this, boost::asio::placeholders::error));
            break;

        case KEEP_ALIVE:
            read_command();
            break;

        case NAME:
            async_read(socket, buffer(&four_bytes, sizeof(four_bytes)), boost::bind(&client::name_id_read, this, boost::asio::placeholders::error));
            break;

        case LEFT:
            async_read(socket, buffer(&four_bytes, sizeof(four_bytes)), boost::bind(&client::left_id_read, this, boost::asio::placeholders::error));
            break;

        case CHAT:
            async_read(socket, buffer(&four_bytes, sizeof(four_bytes)), boost::bind(&client::message_id_read, this, boost::asio::placeholders::error));
            break;

        case PLAYER_RANGE:
            async_read(socket, buffer(&one_byte, sizeof(one_byte)), boost::bind(&client::player_start_read, this, boost::asio::placeholders::error));
            break;

        case CONTROLLERS:
            async_read(socket, buffer(incoming_controls), boost::bind(&client::controllers_read, this, boost::asio::placeholders::error));
            break;

        case START_GAME:
            my_game.game_has_started();
            read_command();
            break;

        case INPUT_DATA:
            async_read(socket, buffer(incoming_input), boost::bind(&client::input_read, this, boost::asio::placeholders::error));
            break;

        case LAG:
            async_read(socket, buffer(&one_byte, sizeof(one_byte)), boost::bind(&client::lag_read, this, boost::asio::placeholders::error));
            break;

        default:
            read_command();
            break;
    }
}

void client::controllers_read(const boost::system::error_code& error) {
    if (error) {
        handle_error(error, true);
        return;
    }

    my_game.update_netplay_controllers(incoming_controls);
    incoming_input.resize(my_game.get_remote_count());

    read_command();
}

void client::player_start_read(const boost::system::error_code& error) {
    if (error) {
        handle_error(error, true);
        return;
    }

    my_game.set_player_start(one_byte);

    async_read(socket, buffer(&one_byte, sizeof(one_byte)), boost::bind(&client::player_count_read, this, boost::asio::placeholders::error));
}

void client::player_count_read(const boost::system::error_code& error) {
    if (error) {
        handle_error(error, true);
        return;
    }

    my_game.set_player_count(one_byte);

    read_command();
}

void client::server_version_read(const boost::system::error_code& error) {
    if (error) {
        handle_error(error, true);
        return;
    }

    if (two_bytes != MY_VERSION) {
        stop();
        my_dialog.error(L"Server version does not match client version.");

        return;
    }

    read_command();
}

void client::name_id_read(const boost::system::error_code& error) {
    if (error) {
        handle_error(error, true);
        return;
    }

    async_read(socket, buffer(&one_byte, sizeof(one_byte)), boost::bind(&client::name_length_read, this, boost::asio::placeholders::error));
}

void client::name_length_read(const boost::system::error_code& error) {
    if (error) {
        handle_error(error, true);
        return;
    }

    incoming_text.resize(one_byte);
    async_read(socket, buffer(incoming_text), boost::bind(&client::name_read, this, boost::asio::placeholders::error));
}

void client::name_read(const boost::system::error_code& error) {
    if (error) {
        handle_error(error, true);
        return;
    }

    wstring incoming_name;
    incoming_name.assign(incoming_text.begin(), incoming_text.end());

    my_game.name_change(four_bytes, incoming_name);

    read_command();
}

void client::left_id_read(const boost::system::error_code& error) {
    if (error) {
        handle_error(error, true);
        return;
    }

    my_game.name_left(four_bytes);

    read_command();
}

void client::message_id_read(const boost::system::error_code& error) {
    if (error) {
        handle_error(error, true);
        return;
    }

    async_read(socket, buffer(&two_bytes, sizeof(two_bytes)), boost::bind(&client::message_length_read, this, boost::asio::placeholders::error));
}

void client::message_length_read(const boost::system::error_code& error) {
    if (error) {
        handle_error(error, true);
        return;
    }

    incoming_text.resize(two_bytes);

    async_read(socket, buffer(incoming_text), boost::bind(&client::message_read, this, boost::asio::placeholders::error));
}

void client::message_read(const boost::system::error_code& error) {
    if (error) {
        handle_error(error, true);
        return;
    }

    wstring incoming_message;
    incoming_message.assign(incoming_text.begin(), incoming_text.end());

    my_game.chat_received(four_bytes, incoming_message);

    read_command();
}

void client::lag_read(const boost::system::error_code& error) {
    if (error) {
        handle_error(error, true);
        return;
    }

    my_game.set_lag(one_byte);

    read_command();
}

void client::input_read(const boost::system::error_code& error) {
    if (error) {
        handle_error(error, true);
        return;
    }

    my_game.incoming_remote_input(incoming_input);

    read_command();
}

void client::send_version() {
    if (!is_connected) {
        return;
    }

    send(packet() << VERSION << MY_VERSION);
}

void client::send_keep_alive() {
    if (!is_connected) {
        return;
    }

    send(packet() << KEEP_ALIVE);
}

void client::send_name(const wstring& name) {
    if (!is_connected) {
        return;
    }

    packet p;
    p << NAME;
    p << (uint8_t) name.size();
    for (wstring::const_iterator it = name.begin(); it != name.end(); ++it) {
        p << *it;
    }

    send(p);
}

void client::send_chat(const wstring& message) {
    if (!is_connected) {
        return;
    }

    packet p;
    p << CHAT;
    p << (uint16_t) message.size();
    for (wstring::const_iterator it = message.begin(); it != message.end(); ++it) {
        p << *it;
    }

    send(p);
}

void client::send_controllers(const vector<CONTROL>& controllers) {
    if (!is_connected) {
        return;
    }

    send(packet() << CONTROLLERS << controllers);
}

void client::send_start_game() {
    if (!is_connected) {
        my_dialog.error(L"Cannot start game unless connected to server.");
        return;
    }

    send(packet() << START_GAME);
}

void client::send_lag(uint8_t lag) {
    if (!is_connected) {
        return;
    }

    send(packet() << LAG << lag);
}

void client::send_input(const vector<BUTTONS>& input) {
    if (!is_connected) {
        return;
    }

    send(packet() << INPUT_DATA << input);
}

void client::send(const packet& p) {
    out_buffer.push_back(p);

    if (output.empty()) {
        begin_send();
    }
}

void client::begin_send() {
    while (!out_buffer.empty()) {
        output << out_buffer.front();
        out_buffer.pop_front();
    }

    async_write(socket, buffer(output.data()), boost::bind(&client::data_sent, this, boost::asio::placeholders::error));
}

void client::data_sent(const boost::system::error_code& error) {
    output.clear();

    if (error) {
        handle_error(error, true);
        return;
    }

    if (!out_buffer.empty()) {
        begin_send();
    }
}
