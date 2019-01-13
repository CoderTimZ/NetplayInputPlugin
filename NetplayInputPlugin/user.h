#pragma once

#include "stdafx.h"

#include "common.h"
#include "connection.h"
#include "server.h"
#include "room.h"
#include "packet.h"
#include "controller_map.h"

class user : public std::enable_shared_from_this<user> {
    public:
        user(std::shared_ptr<asio::io_service> io_service, std::shared_ptr<server> server);
        bool joined();
        void set_room(room_ptr my_room);
        uint32_t get_id() const;
        const std::string& get_name() const;
        double get_latency() const;
        double get_median_latency() const;
        const std::array<controller, 4>& get_controllers() const;
        bool is_player() const;
        bool is_spectator() const;
        double get_fps();
        void process_packet();
        void send_protocol_version();
        void send_accept();
        void send_join(uint32_t user_id, const std::string& name);
        void send_input(const user& user, const packet& p);
        void send_name(uint32_t id, const std::string& name);
        void send_ping();
        void send_quit(uint32_t id);
        void send_message(int32_t id, const std::string& message);
        void send_status(const std::string& message);
        void send_error(const std::string& message);
        void send_start_game();
        void send_lag(uint8_t lag);
        void send_hia(uint32_t hia);

    private:
        std::function<void(const asio::error_code&)> error_handler();

    private:
        std::shared_ptr<server> my_server;
        std::shared_ptr<room> my_room;
        std::shared_ptr<connection> conn;
        std::string address;
        uint32_t id;
        std::string name;
        std::array<controller, 4> controllers;
        controller_map my_controller_map;
        std::list<double> frame_history;
        std::list<double> latency_history;
        uint32_t input_received = 0;
        bool manual_map = false;
        std::vector<uint8_t> current_input;
        packet pout;

        friend class room;
        friend class server;

        static uint32_t next_id;
};
