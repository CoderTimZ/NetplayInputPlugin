#pragma once

#include <cstdint>
#include <map>
#include <vector>
#include <chrono>
#include <thread>
#include <list>
#include <asio.hpp>

#include "client_server_common.h"
#include "packet.h"
#include "controller.h"

class session;
typedef std::shared_ptr<session> session_ptr;

class server: public std::enable_shared_from_this<server> {
    public:
        server(asio::io_service& io_s, uint8_t lag);
        ~server();

        uint16_t start(uint16_t port);
        uint64_t time();
        int player_count();
        void stop();
    private:
        void accept();
        void on_tick(const asio::error_code& error);
        void on_session_joined(session_ptr session);
        void on_session_quit(session_ptr session);
        void update_controllers();
        void send_input(uint32_t id, uint8_t port, controller::BUTTONS buttons);
        void send_name(uint32_t id, const std::string& name);
        void send_message(int32_t id, const std::string& message);
        void send_lag(int32_t id, uint8_t lag);
        void send_start_game();
        void send_latencies();
        int32_t get_total_latency();

        asio::io_service& io_s;
        asio::ip::tcp::acceptor acceptor;
        asio::steady_timer timer;

        std::chrono::high_resolution_clock::time_point start_time;
        uint32_t next_id;
        bool game_started;
        uint8_t lag;
        bool autolag = true;
        std::map<uint32_t, session_ptr> sessions;
        std::array<controller::CONTROL, MAX_PLAYERS> netplay_controllers;

        friend class session;
};
