#pragma once

#include "stdafx.h"

#include "common.h"
#include "packet.h"
#include "controller.h"

class user;
typedef std::shared_ptr<user> user_ptr;

class server;
typedef std::shared_ptr<server> server_ptr;

class room: public std::enable_shared_from_this<room> {
    public:
        room(const std::string& id, server_ptr my_server);

        const std::string& get_id() const;
        user_ptr get_user(uint32_t id);
        int player_count(int32_t excluding);
        void close();
        void on_tick();
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
        double get_total_latency();
        double get_fps();
        void auto_adjust_lag();

        const std::string id;
        const server_ptr my_server;
        bool started;
        uint8_t lag = 5;
        bool autolag = true;
        std::vector<user_ptr> users;

        friend class user;
};
