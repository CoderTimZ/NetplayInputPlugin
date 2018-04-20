#pragma once

#include <boost/asio.hpp>
#include <boost/enable_shared_from_this.hpp>
#include <vector>

#include "Controller 1.0.h"
#include "packet.h"

class server;

class session: public boost::enable_shared_from_this<session> {
    public:
        session(server& my_server, uint32_t id);

        void stop();

        uint32_t get_id() const;
        const std::vector<wchar_t>& get_name() const;
        const int get_latency() const;
        const std::vector<CONTROL>& get_controllers() const;
        bool is_player();
        bool is_ping_pending();

        void read_command();

        void send_input(uint8_t start, const std::vector<BUTTONS>& buttons);
        void send_protocol_version();
        void send_name(uint32_t id, const std::vector<wchar_t>& name);
        void send_ping();
        void cancel_ping();
        void send_departure(uint32_t id);
        void send_message(uint32_t id, const std::vector<wchar_t>& message);
        void send_controller_range(uint8_t player_start, uint8_t player_count);

        void send_controllers(const std::vector<CONTROL>& controllers);
        void send_start_game();
        void send_lag(uint8_t lag);
        void send(const packet& p);

    private:
        void on_command(const boost::system::error_code& error);
        void on_input(const boost::system::error_code& error);
        void on_client_protocol_version(const boost::system::error_code& error);
        void on_pong(const boost::system::error_code& error);
        void on_name_length(const boost::system::error_code& error);
        void on_name(const boost::system::error_code& error);
        void on_controllers(const boost::system::error_code& error);
        void on_message_length(const boost::system::error_code& error);
        void on_message(const boost::system::error_code& error);
        void on_lag(const boost::system::error_code& error);

        void begin_send();
        void on_data_sent(const boost::system::error_code& error);

        bool is_me(uint8_t controller_id);
        bool ready_to_send_input();
        void send_input();

        void handle_error(const boost::system::error_code& error);

    public:
        boost::asio::ip::tcp::socket socket;

    private:
        LARGE_INTEGER time_of_ping;

        // Initialized in constructor
        server& my_server;
        uint32_t id;
        uint32_t next_ping_id;
        uint32_t pending_ping_id;

        // Read from client
        std::vector<wchar_t> name;
        int32_t latency = -1;
        std::vector<CONTROL> controllers;

        // Determined by server
        uint8_t player_start;
        uint8_t total_players;

        // Temp input variables
        uint8_t one_byte;
        uint16_t two_bytes;
        uint32_t four_bytes;
        std::vector<BUTTONS> in_buttons;
        std::vector<wchar_t> text;

        // Output
        std::vector<std::list<BUTTONS>> out_buttons;
        std::list<packet> out_buffer;
        packet output;
};
