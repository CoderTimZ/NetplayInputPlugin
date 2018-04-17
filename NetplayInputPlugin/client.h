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
        void send_version();
        void send_keep_alive();
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
        uint32_t four_bytes;
        std::vector<wchar_t> incoming_text;
        std::vector<CONTROL> incoming_controls;
        std::vector<BUTTONS> incoming_input;

        void stop();

        void handle_error(const boost::system::error_code& error, bool lost_connection);

        void resolved(const boost::system::error_code& error, boost::asio::ip::tcp::resolver::iterator iterator);
        void begin_connect(boost::asio::ip::tcp::resolver::iterator iterator);
        void connected(const boost::system::error_code& error, boost::asio::ip::tcp::resolver::iterator iterator);

        void read_command();
        void command_read(const boost::system::error_code& error);

        void server_version_read(const boost::system::error_code& error);

        void name_id_read(const boost::system::error_code& error);
        void name_length_read(const boost::system::error_code& error);
        void name_read(const boost::system::error_code& error);

        void left_id_read(const boost::system::error_code& error);

        void message_id_read(const boost::system::error_code& error);
        void message_length_read(const boost::system::error_code& error);
        void message_read(const boost::system::error_code& error);

        void player_start_read(const boost::system::error_code& error);
        void player_count_read(const boost::system::error_code& error);

        void controllers_read(const boost::system::error_code& error);

        void input_read(const boost::system::error_code& error);

        void lag_read(const boost::system::error_code& error);

        void send(const packet& p);
        void begin_send();
        void data_sent(const boost::system::error_code& error);
};
