#pragma once

#include "stdafx.h"

#include "common.h"
#include "packet.h"

class user;
class server;

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
        room(const std::string& id, std::shared_ptr<server> my_server);

        const std::string& get_id() const;
        std::shared_ptr<user> get_user(uint32_t id);
        void close();
        size_t player_count() const;
        void on_tick();
        void on_input_tick();
        void on_user_join(std::shared_ptr<user> user);
        void on_user_quit(std::shared_ptr<user> user);

        const double creation_timestamp = timestamp();

    private:
        void on_game_start();
        void update_controller_map();
        void send_controllers();
        void send_info(const std::string& message);
        void send_error(const std::string& message);
        void send_lag(int32_t id, uint8_t lag);
        void send_latencies();
        double get_latency();
        double get_fps();
        void auto_adjust_lag();

        const std::string id;
        std::weak_ptr<server> my_server;
        std::vector<std::shared_ptr<user>> users;
        bool started;
        uint8_t lag = 5;
        bool autolag = true;
        bool golf = false;
        packet pout;
        uint32_t hia = 0;
        asio::steady_timer timer;
        std::chrono::time_point<std::chrono::steady_clock> next_input_tick;

        friend class user;
};
