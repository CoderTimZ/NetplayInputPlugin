#pragma once

#include <stdint.h>
#include <boost/shared_ptr.hpp>
#include <boost/asio.hpp>
#include <boost/thread.hpp>
#include <map>
#include <vector>

#include "Controller 1.0.h"

class client_dialog;

class session;
typedef boost::shared_ptr<session> session_ptr;

class server {
    public:
        server(client_dialog& my_dialog, uint8_t lag);
        ~server();

        uint16_t start(uint16_t port);
    private:
        void stop();

        void begin_accept();
        void accepted(session_ptr s, const boost::system::error_code& error);

        void remove_session(uint32_t id);
        void send_input(uint32_t id, uint8_t start, const std::vector<BUTTONS> buttons);
        void send_name(uint32_t id, const std::vector<wchar_t>& name);
        void send_message(uint32_t id, const std::vector<wchar_t>& message);
        void send_lag(uint32_t id, uint8_t lag);
        void send_start_game();

        client_dialog& my_dialog;
        boost::asio::io_service io_s;
        boost::asio::io_service::work work;
        boost::asio::ip::tcp::acceptor acceptor;
        boost::thread thread;

        uint32_t next_id;
        bool game_started;
        uint8_t lag;


        std::map<uint32_t, session_ptr> sessions;

        friend class session;
};
