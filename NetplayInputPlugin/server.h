#pragma once

#include "stdafx.h"

#include "client_server_common.h"
#include "packet.h"
#include "controller.h"

class session;
typedef std::shared_ptr<session> session_ptr;

class server: public std::enable_shared_from_this<server> {
    public:
        server(std::shared_ptr<asio::io_service> io_s, uint8_t lag);

        uint16_t open(uint16_t port);
        uint64_t time();
        int player_count();
        void close();
    private:
        void accept();
        void on_tick(const asio::error_code& error);
        void on_session_joined(session_ptr session);
        void on_session_quit(session_ptr session);
        void update_controllers();
        void send_input(uint32_t id, uint8_t port, input input);
        void send_name(uint32_t id, const std::string& name);
        void send_message(int32_t id, const std::string& message);
        void send_lag(int32_t id, uint8_t lag);
        void send_start_game();
        void send_latencies();
        int32_t get_total_latency();
        int32_t get_fps();
        void auto_adjust_lag();

        std::shared_ptr<asio::io_service> io_s;
        asio::ip::tcp::acceptor acceptor;
        asio::steady_timer timer;

        std::chrono::high_resolution_clock::time_point start_time;
        uint32_t next_id;
        bool game_started;
        uint8_t lag;
        bool autolag = true;
        std::map<uint32_t, session_ptr> sessions;
        std::array<controller, MAX_PLAYERS> netplay_controllers;

        friend class session;
};
