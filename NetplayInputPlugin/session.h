#pragma once

#include <boost/asio.hpp>
#include <vector>

#include "Controller 1.0.h"
#include "packet.h"

class server;

class session: public std::enable_shared_from_this<session> {
    public:
        session(server& my_server, uint32_t id);

        void stop();

        uint32_t get_id() const;
        const std::wstring& get_name() const;
        const int get_latency() const;
        const std::vector<CONTROL>& get_controllers() const;
        bool is_player();
        bool is_pong_pending();

        void read_command();

        void send_input(uint8_t start, const std::vector<BUTTONS>& buttons);
        void send_protocol_version();
        void send_name(uint32_t id, const std::wstring& name);
        void send_ping();
        void cancel_ping();
        void send_departure(uint32_t id);
        void send_message(uint32_t id, const std::wstring& message);
        void send_controller_range(uint8_t player_index, uint8_t player_count);

        void send_controllers(const std::vector<CONTROL>& controllers);
        void send_start_game();
        void send_lag(uint8_t lag);
        void send(const packet& p);

    private:
        void handle_error(const boost::system::error_code& error);
        bool is_me(uint8_t controller_id);
        bool ready_to_send_input();
        void send_input();
        void flush();

    public:
        boost::asio::ip::tcp::socket socket;

    private:
        LARGE_INTEGER time_of_ping;

        // Initialized in constructor
        server& my_server;
        uint32_t id;
        int32_t next_ping_id;
        int32_t pending_pong_id;

        // Read from client
        std::wstring name;
        int32_t latency = -1;
        std::vector<CONTROL> controllers;

        // Determined by server
        uint8_t player_index;

        // Temp input variables
        std::vector<BUTTONS> in_buttons;

        // Output
        std::vector<std::list<BUTTONS>> output_buttons;
        std::list<packet> output_queue;
        packet output_buffer;
};
