#pragma once

#include "stdafx.h"

#include "common.h"
#include "connection.h"
#include "server.h"
#include "room.h"
#include "packet.h"
#include "controller_map.h"

class user: public connection {
    public:
        user(std::shared_ptr<asio::io_service> io_s, std::shared_ptr<server> my_server);

        std::shared_ptr<user> shared_from_this() {
            return std::static_pointer_cast<user>(connection::shared_from_this());
        }

        bool joined();
        void set_room(room_ptr my_room);
        uint32_t get_id() const;
        const std::string& get_name() const;
        double get_latency() const;
        double get_median_latency() const;
        const std::array<controller, 4>& get_controllers() const;
        bool is_player() const;
        double get_fps();
        void process_packet();
        void send_protocol_version();
        void send_accept();
        void send_join(uint32_t user_id, const std::string& name);
        void send_input(uint32_t user_id, input buttons[4]);
        void send_name(uint32_t id, const std::string& name);
        void send_ping();
        void send_quit(uint32_t id);
        void send_message(int32_t id, const std::string& message);
        void send_status(const std::string& message);
        void send_error(const std::string& message);
        void send_start_game();
        void send_lag(uint8_t lag);

    private:
        void handle_error(const asio::error_code& error);

    private:
        std::shared_ptr<server> my_server;
        std::shared_ptr<room> my_room;
        std::string address;
        uint32_t id;
        std::string name;
        std::array<controller, 4> controllers;
        controller_map my_controller_map;
        std::deque<double> frame_history;
        std::list<double> latency_history;
        bool manual_map = false;

        friend class room;
        friend class server;

        static uint32_t next_id;
};
