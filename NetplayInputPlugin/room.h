#pragma once

#include "stdafx.h"

#include "common.h"
#include "packet.h"

class user;
class server;

class room: public std::enable_shared_from_this<room> {
    public:
        room(const std::string& id, server* server, rom_info rom);

        const std::string& get_id() const;
        void close();
        void on_ping_tick();
        void on_user_join(user* user);
        void on_user_quit(user* user);

        const double creation_timestamp = timestamp();

    private:
        void on_game_start();
        void update_controller_map();
        double get_latency() const;
        double get_input_rate() const;
        void auto_adjust_lag();
        void send_controllers();
        void send_info(const std::string& message);
        void send_error(const std::string& message);
        void set_lag(uint8_t lag, user* source);
        void send_latencies();

        const std::string id;
        server* my_server;
        std::vector<user*> user_map;
        std::vector<user*> user_list;
        rom_info rom;
        bool started = false;
        uint8_t lag = 5;
        bool autolag = true;
        bool golf = false;

        friend class user;
        friend class server;
};
