#pragma once

#include <cstdint>
#include <vector>
#include <deque>
#include <string>
#include <list>
#include <memory>
#include <boost/asio.hpp>

#include "server.h"
#include "packet.h"

class session: public std::enable_shared_from_this<session> {
    public:
        session(std::shared_ptr<server> my_server, uint32_t id);

        void stop();

        uint32_t get_id() const;
        const std::string& get_name() const;
        int32_t get_latency() const;
        int32_t get_average_latency() const;
        const std::vector<controller::CONTROL>& get_controllers() const;
        bool is_player();
        uint32_t get_fps();

        void next_packet();

        void send_input(uint8_t start, const std::vector<controller::BUTTONS>& buttons);
        void send_protocol_version();
        void send_name(uint32_t id, const std::string& name);
        void send_ping(uint64_t time);
        void send_departure(uint32_t id);
        void send_message(int32_t id, const std::string& message);
        void send_controller_range(uint8_t player_index, uint8_t player_count);

        void send_controllers(const std::vector<controller::CONTROL>& controllers);
        void send_start_game();
        void send_lag(uint8_t lag);
        void send(const packet& p);

    private:
        void handle_error(const boost::system::error_code& error);
        bool is_me(uint32_t controller_id);
        bool ready_to_send_input();
        void send_input();
        void flush();

    public:
        boost::asio::ip::tcp::socket socket;

    private:
        // Initialized in constructor
        std::shared_ptr<server> my_server;
        uint32_t id;

        // Read from client
        std::string name;
        std::vector<controller::CONTROL> controllers;
        std::deque<std::tuple<uint32_t, uint64_t>> frame_history;
        std::deque<uint32_t> latency_history;

        // Determined by server
        uint8_t player_index;

        // Temp input variables
        std::vector<controller::BUTTONS> in_buttons;

        // Output
        std::vector<std::list<controller::BUTTONS>> output_buttons;
        std::list<packet> output_queue;
        packet output_buffer;
};
