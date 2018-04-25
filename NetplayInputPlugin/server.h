#pragma once

#include <cstdint>
#include <map>
#include <vector>
#include <chrono>
#include <thread>
#include <list>
#include <boost/asio.hpp>

#include "packet.h"
#include "controller.h"

class session;
typedef std::shared_ptr<session> session_ptr;

class server: public std::enable_shared_from_this<server> {
    public:
        server(boost::asio::io_service& io_s, uint8_t lag);
        ~server();

        uint16_t start(uint16_t port);
        void stop();
        uint64_t get_time();
    private:
        void accept();
        void on_tick(const boost::system::error_code& error);
        void remove_session(uint32_t id);
        void send_input(uint32_t id, uint8_t start, uint32_t frame, const std::vector<controller::BUTTONS> buttons);
        void send_name(uint32_t id, const std::string& name);
        void send_message(int32_t id, const std::string& message);
        void send_lag(int32_t id, uint8_t lag);
        void send_start_game();
        void send_latencies();
        int32_t get_total_latency();

        boost::asio::io_service& io_s;
        boost::asio::ip::tcp::acceptor acceptor;
        boost::asio::steady_timer timer;

        std::chrono::high_resolution_clock::time_point start_time;
        uint32_t next_id;
        bool game_started;
        uint8_t lag;
        bool autolag = true;
        std::map<uint32_t, session_ptr> sessions;

        friend class session;
};
