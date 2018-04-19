#pragma once

#include <stdint.h>
#include <boost/thread.hpp>
#include <boost/asio.hpp>
#include <boost/shared_ptr.hpp>
#include <list>
#include <map>
#include <string>

#include "server.h"
#include "client_server_common.h"
#include "packet.h"

class game;
class client_dialog;

class client {
    public:
        boost::asio::io_service io_s;

        client(client_dialog& my_dialog, game& my_game);
        ~client();

        void connect(const std::wstring& host, uint16_t port);
        void send_protocol_version();
        void send_name(const std::wstring& name);
        void send_controllers(const std::vector<CONTROL>& controllers);
        void send_chat(const std::wstring& message);
        void send_start_game();
        void send_lag(uint8_t lag);
        void send_input(const std::vector<BUTTONS>& input);

    private:
        client_dialog& my_dialog;
        game& my_game;

        boost::asio::io_service::work work;
        boost::asio::ip::tcp::resolver resolver;
        boost::asio::ip::tcp::socket socket;
        boost::thread thread;

        bool is_connected;
        std::list<packet> out_buffer;
        packet output;

        uint8_t one_byte;
        uint16_t two_bytes;
        uint32_t four_bytes, four_bytes2, four_bytes3;
        std::vector<wchar_t> incoming_text;
        std::vector<CONTROL> incoming_controls;
        std::vector<BUTTONS> incoming_input;

        void stop();

        void handle_error(const boost::system::error_code& error, bool lost_connection);

        void read_command();
        void begin_connect(boost::asio::ip::tcp::resolver::iterator iterator);

        void on_resolve(const boost::system::error_code& error, boost::asio::ip::tcp::resolver::iterator iterator);
        void on_connect(const boost::system::error_code& error, boost::asio::ip::tcp::resolver::iterator iterator);
        void on_command(const boost::system::error_code& error);
        void on_server_protocol_version(const boost::system::error_code& error);
        void on_ping(const boost::system::error_code& error);
        void on_latency_user_count(const boost::system::error_code& error);
        void on_latency_user_id(const boost::system::error_code& error);
        void on_latency_time(const boost::system::error_code& error);
        void on_name_user_id(const boost::system::error_code& error);
        void on_name_length(const boost::system::error_code& error);
        void on_name(const boost::system::error_code& error);
        void on_removed_user_id(const boost::system::error_code& error);
        void on_message_user_id(const boost::system::error_code& error);
        void on_message_length(const boost::system::error_code& error);
        void on_message(const boost::system::error_code& error);
        void on_player_start_index(const boost::system::error_code& error);
        void on_player_count(const boost::system::error_code& error);
        void on_controllers(const boost::system::error_code& error);
        void on_input(const boost::system::error_code& error);
        void on_lag(const boost::system::error_code& error);

        void send(const packet& p);
        void begin_send();
        void on_data_sent(const boost::system::error_code& error);
};
