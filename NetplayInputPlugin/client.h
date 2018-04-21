#pragma once

#include <cstdint>
#include <boost/thread.hpp>
#include <boost/asio.hpp>
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
        std::list<packet> output_queue;
        packet output_buffer;

        void stop();
        void handle_error(const boost::system::error_code& error, bool lost_connection);
        void read_command();
        void send(const packet& p);
        void flush();
};
