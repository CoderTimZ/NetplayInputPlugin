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
                           boost::bind(&client::on_resolve, this, boost::asio::placeholders::error, boost::asio::placeholders::iterator));
}

void client::on_resolve(const boost::system::error_code& error, ip::tcp::resolver::iterator iterator) {
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
    socket.async_connect(endpoint, boost::bind(&client::on_connect, this, boost::asio::placeholders::error, ++iterator));
}

void client::on_connect(const boost::system::error_code& error, ip::tcp::resolver::iterator iterator) {
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

    send_protocol_version();
    send_name(my_game.get_name());
    send_controllers(my_game.get_local_controllers());

    my_dialog.status(L"Connected!");
}

void client::read_command() {
    async_read(socket, buffer(&one_byte, sizeof(one_byte)), boost::bind(&client::on_command, this, boost::asio::placeholders::error));
}

void client::on_command(const boost::system::error_code& error) {
    if (error) {
        handle_error(error, true);
        return;
    }

    switch (one_byte) {
        case PROTOCOL_VERSION:
            async_read(socket, buffer(&two_bytes, sizeof(two_bytes)), boost::bind(&client::on_server_protocol_version, this, boost::asio::placeholders::error));
            break;

        case PING:
            async_read(socket, buffer(&four_bytes, sizeof(four_bytes)), boost::bind(&client::on_ping, this, boost::asio::placeholders::error));
            break;

        case LATENCIES:
            async_read(socket, buffer(&four_bytes, sizeof(four_bytes)), boost::bind(&client::on_latency_user_count, this, boost::asio::placeholders::error));
            break;

        case NAME:
            async_read(socket, buffer(&four_bytes, sizeof(four_bytes)), boost::bind(&client::on_name_user_id, this, boost::asio::placeholders::error));
            break;

        case LEFT:
            async_read(socket, buffer(&four_bytes, sizeof(four_bytes)), boost::bind(&client::on_removed_user_id, this, boost::asio::placeholders::error));
            break;

        case CHAT:
            async_read(socket, buffer(&four_bytes, sizeof(four_bytes)), boost::bind(&client::on_message_user_id, this, boost::asio::placeholders::error));
            break;

        case PLAYER_RANGE:
            async_read(socket, buffer(&one_byte, sizeof(one_byte)), boost::bind(&client::on_player_start_index, this, boost::asio::placeholders::error));
            break;

        case CONTROLLERS:
            async_read(socket, buffer(incoming_controls), boost::bind(&client::on_controllers, this, boost::asio::placeholders::error));
            break;

        case START_GAME:
            my_game.game_has_started();
            read_command();
            break;

        case INPUT_DATA:
            async_read(socket, buffer(incoming_input), boost::bind(&client::on_input, this, boost::asio::placeholders::error));
            break;

        case LAG:
            async_read(socket, buffer(&one_byte, sizeof(one_byte)), boost::bind(&client::on_lag, this, boost::asio::placeholders::error));
            break;

        default:
            read_command();
            break;
    }
}

void client::on_controllers(const boost::system::error_code& error) {
    if (error) {
        handle_error(error, true);
        return;
    }

    my_game.update_netplay_controllers(incoming_controls);
    incoming_input.resize(my_game.get_remote_count());

    read_command();
}

void client::on_player_start_index(const boost::system::error_code& error) {
    if (error) {
        handle_error(error, true);
        return;
    }

    my_game.set_player_start(one_byte);

    async_read(socket, buffer(&one_byte, sizeof(one_byte)), boost::bind(&client::on_player_count, this, boost::asio::placeholders::error));
}

void client::on_player_count(const boost::system::error_code& error) {
    if (error) {
        handle_error(error, true);
        return;
    }

    my_game.set_player_count(one_byte);

    read_command();
}

void client::on_server_protocol_version(const boost::system::error_code& error) {
    if (error) {
        handle_error(error, true);
        return;
    }

    if (two_bytes != MY_PROTOCOL_VERSION) {
        stop();
        my_dialog.error(L"Server protocol version does not match client protocol version.");

        return;
    }

    read_command();
}

void client::on_ping(const boost::system::error_code& error) {
    if (error) {
        handle_error(error, true);
        return;
    }

    send(packet() << PONG << four_bytes);

    read_command();
}

void client::on_latency_user_count(const boost::system::error_code& error) {
    if (error) {
        handle_error(error, true);
        return;
    }

    if (four_bytes > 0) {
        async_read(socket, buffer(&four_bytes2, sizeof(four_bytes2)), boost::bind(&client::on_latency_user_id, this, boost::asio::placeholders::error));
    } else {
        read_command();
    }
}

void client::on_latency_user_id(const boost::system::error_code& error) {
    if (error) {
        handle_error(error, true);
        return;
    }

    async_read(socket, buffer(&four_bytes3, sizeof(four_bytes3)), boost::bind(&client::on_latency_time, this, boost::asio::placeholders::error));
}

void client::on_latency_time(const boost::system::error_code& error) {
    if (error) {
        handle_error(error, true);
        return;
    }

    if ((int32_t)four_bytes3 >= 0) {
        my_game.set_user_latency(four_bytes2, four_bytes3);
    }
    four_bytes--;

    if (four_bytes > 0) {
        async_read(socket, buffer(&four_bytes2, sizeof(four_bytes2)), boost::bind(&client::on_latency_user_id, this, boost::asio::placeholders::error));
    } else {
        my_dialog.update_user_list(my_game.get_names(), my_game.get_latencies());

        read_command();
    }
}

void client::on_name_user_id(const boost::system::error_code& error) {
    if (error) {
        handle_error(error, true);
        return;
    }

    async_read(socket, buffer(&one_byte, sizeof(one_byte)), boost::bind(&client::on_name_length, this, boost::asio::placeholders::error));
}

void client::on_name_length(const boost::system::error_code& error) {
    if (error) {
        handle_error(error, true);
        return;
    }

    incoming_text.resize(one_byte);
    async_read(socket, buffer(incoming_text), boost::bind(&client::on_name, this, boost::asio::placeholders::error));
}

void client::on_name(const boost::system::error_code& error) {
    if (error) {
        handle_error(error, true);
        return;
    }

    wstring incoming_name;
    incoming_name.assign(incoming_text.begin(), incoming_text.end());

    my_game.set_user_name(four_bytes, incoming_name);

    read_command();
}

void client::on_removed_user_id(const boost::system::error_code& error) {
    if (error) {
        handle_error(error, true);
        return;
    }

    my_game.remove_user(four_bytes);

    read_command();
}

void client::on_message_user_id(const boost::system::error_code& error) {
    if (error) {
        handle_error(error, true);
        return;
    }

    async_read(socket, buffer(&two_bytes, sizeof(two_bytes)), boost::bind(&client::on_message_length, this, boost::asio::placeholders::error));
}

void client::on_message_length(const boost::system::error_code& error) {
    if (error) {
        handle_error(error, true);
        return;
    }

    incoming_text.resize(two_bytes);

    async_read(socket, buffer(incoming_text), boost::bind(&client::on_message, this, boost::asio::placeholders::error));
}

void client::on_message(const boost::system::error_code& error) {
    if (error) {
        handle_error(error, true);
        return;
    }

    wstring incoming_message;
    incoming_message.assign(incoming_text.begin(), incoming_text.end());

    my_game.chat_received(four_bytes, incoming_message);

    read_command();
}

void client::on_lag(const boost::system::error_code& error) {
    if (error) {
        handle_error(error, true);
        return;
    }

    my_game.set_lag(one_byte);

    read_command();
}

void client::on_input(const boost::system::error_code& error) {
    if (error) {
        handle_error(error, true);
        return;
    }

    my_game.incoming_remote_input(incoming_input);

    read_command();
}

void client::send_protocol_version() {
    if (!is_connected) {
        return;
    }

    send(packet() << PROTOCOL_VERSION << MY_PROTOCOL_VERSION);
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

    async_write(socket, buffer(output.data()), boost::bind(&client::on_data_sent, this, boost::asio::placeholders::error));
}

void client::on_data_sent(const boost::system::error_code& error) {
    output.clear();

    if (error) {
        handle_error(error, true);
        return;
    }

    if (!out_buffer.empty()) {
        begin_send();
    }
}
