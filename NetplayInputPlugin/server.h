#pragma once

#include <cstdint>
#include <boost/asio.hpp>
#include <boost/thread.hpp>
#include <map>
#include <vector>

#include "packet.h"
#include "Controller 1.0.h"

class client_dialog;

class session;
typedef std::shared_ptr<session> session_ptr;

class server {
    public:
        server(client_dialog& my_dialog, uint8_t lag);
        ~server();

        uint16_t start(uint16_t port);
    private:
        void stop();
        void accept();
        void on_tick(const boost::system::error_code& error);
        void remove_session(uint32_t id);
        void send_input(uint32_t id, uint8_t start, uint32_t frame, const std::vector<BUTTONS> buttons);
        void send_name(uint32_t id, const std::wstring& name);
        void send_message(int32_t id, const std::wstring& message);
        void send_lag(int32_t id, uint8_t lag);
        void send_start_game();
        void send_latencies();
        int32_t get_total_latency();

        client_dialog& my_dialog;
        boost::asio::io_service io_s;
        boost::asio::io_service::work work;
        boost::asio::ip::tcp::acceptor acceptor;
        boost::asio::steady_timer timer;
        std::thread thread;

        uint32_t next_id;
        bool game_started;
        uint8_t lag;
        bool auto_lag = true;
        std::vector<uint32_t> queue_size_history;

        std::map<uint32_t, session_ptr> sessions;

        friend class session;
};
