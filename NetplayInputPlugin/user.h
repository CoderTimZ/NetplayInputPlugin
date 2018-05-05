#pragma once

#include "stdafx.h"

#include "client_server_common.h"
#include "connection.h"
#include "server.h"
#include "room.h"
#include "packet.h"
#include "controller_map.h"

class user: public connection {
    public:
        user(std::shared_ptr<asio::io_service> io_s, std::shared_ptr<server> my_server);
        ~user();

        std::shared_ptr<user> shared_from_this() {
            return std::static_pointer_cast<user>(connection::shared_from_this());
        }

        void set_room(room_ptr my_room);

        uint32_t get_id() const;
        const std::string& get_name() const;
        int32_t get_latency() const;
        int32_t get_minimum_latency() const;
        const std::array<controller, MAX_PLAYERS>& get_controllers() const;
        bool is_player() const;
        size_t get_fps();

        void process_packet();
        void send_join(uint32_t user_id, const std::string& name);
        void send_input(uint8_t port, input input);
        void send_protocol_version();
        void send_name(uint32_t id, const std::string& name);
        void send_ping(uint64_t time);
        void send_quit(uint32_t id);
        void send_message(int32_t id, const std::string& message);
        void send_netplay_controllers(const std::array<controller, MAX_PLAYERS>& controllers);
        void send_start_game();
        void send_lag(uint8_t lag);

    private:
        void handle_error(const asio::error_code& error);

    private:
        std::shared_ptr<server> my_server;
        std::shared_ptr<room> my_room;
        uint32_t id;

        // Read from client
        std::string name;
        std::array<controller, MAX_PLAYERS> controllers;
        controller_map my_controller_map;
        std::deque<uint64_t> frame_history;
        std::deque<uint32_t> latency_history;
        bool joined = false;

        // Output
        int pending_input_data_packets = 0;

        friend class room;

        static uint32_t next_id;
};
