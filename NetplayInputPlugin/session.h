#pragma once

#include <cstdint>
#include <vector>
#include <deque>
#include <string>
#include <list>
#include <memory>
#include <asio.hpp>

#include "client_server_common.h"
#include "server.h"
#include "packet.h"
#include "controller_map.h"

class session: public std::enable_shared_from_this<session> {
    public:
        session(std::shared_ptr<server> my_server, uint32_t id);

        void stop();

        uint32_t get_id() const;
        const std::string& get_name() const;
        int32_t get_latency() const;
        int32_t get_minimum_latency() const;
        const std::array<controller::CONTROL, MAX_PLAYERS>& get_controllers() const;
        bool is_player() const;
        uint32_t get_fps();

        void process_packet();

        void send_input(uint8_t port, controller::BUTTONS buttons);
        void send_protocol_version();
        void send_name(uint32_t id, const std::string& name);
        void send_ping(uint64_t time);
        void send_departure(uint32_t id);
        void send_message(int32_t id, const std::string& message);
        void send_controllers(const std::array<controller::CONTROL, MAX_PLAYERS>& controllers);
        void send_start_game();
        void send_lag(uint8_t lag);
        void send(const packet& p);
        void flush();

    private:
        void handle_error(const asio::error_code& error);

    public:
        asio::ip::tcp::socket socket;

    private:
        // Initialized in constructor
        std::shared_ptr<server> my_server;
        uint32_t id;

        // Read from client
        std::string name;
        std::array<controller::CONTROL, MAX_PLAYERS> controllers;
        controller_map my_controller_map;
        std::deque<uint64_t> frame_history;
        std::deque<uint32_t> latency_history;

        // Output
        int pending_input_data_packets = 0;
        std::list<packet> output_queue;
        packet output_buffer;

        friend class server;
};
