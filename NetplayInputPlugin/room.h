#pragma once

#include "stdafx.h"

#include "common.h"
#include "packet.h"

class user;
typedef std::shared_ptr<user> user_ptr;

class server;
typedef std::shared_ptr<server> server_ptr;

enum PAK_TYPE : uint32_t {
    NONE = 1,
    MEM = 2,
    RUMBLE = 3,
    TRANSFER = 4
};

typedef struct {
    uint32_t present = 0;
    uint32_t raw_data = 0;
    uint32_t plugin = PAK_TYPE::NONE;
} controller;

class room: public std::enable_shared_from_this<room> {
    public:
        room(const std::string& id, server_ptr my_server);

        const std::string& get_id() const;
        user_ptr get_user(uint32_t id);
        void close();
        size_t player_count() const;
        void on_tick();
        void on_input_tick();
        void on_user_join(user_ptr user);
        void on_user_quit(user_ptr user);

        const double creation_timestamp = timestamp();

    private:
        void on_game_start();
        void update_controller_map();
        void send_controllers();
        void send_status(const std::string& message);
        void send_error(const std::string& message);
        void send_lag(int32_t id, uint8_t lag);
        void send_latencies();
        double get_latency();
        double get_fps();
        void auto_adjust_lag();

        const std::string id;
        const server_ptr my_server;
        bool started;
        uint8_t lag = 5;
        bool autolag = true;
        std::vector<user_ptr> users;
        bool golf = false;
        packet pout;
        uint32_t hia = 0;
        asio::steady_timer timer;
        std::chrono::time_point<std::chrono::steady_clock> next_input_tick;

        friend class user;
};
