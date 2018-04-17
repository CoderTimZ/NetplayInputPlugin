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
        const std::vector<CONTROL>& get_controllers() const;
        bool is_player();

        void read_command();

        void send_input(uint8_t start, const std::vector<BUTTONS>& buttons);
        void send_version();
        void send_name(uint32_t id, const std::vector<wchar_t>& name);
        void send_departure(uint32_t id);
        void send_message(uint32_t id, const std::vector<wchar_t>& message);
        void send_controller_range(uint8_t player_start, uint8_t player_count);

        void send_controllers(const std::vector<CONTROL>& controllers);
        void send_start_game();
        void send_lag(uint8_t lag);

    private:
        void command_read(const boost::system::error_code& error);

        void input_read(const boost::system::error_code& error);

        void client_version_read(const boost::system::error_code& error);

        void name_length_read(const boost::system::error_code& error);
        void name_read(const boost::system::error_code& error);

        void controllers_read(const boost::system::error_code& error);

        void message_length_read(const boost::system::error_code& error);
        void message_read(const boost::system::error_code& error);

        void lag_read(const boost::system::error_code& error);

        void send(const packet& p);
        void begin_send();
        void data_sent(const boost::system::error_code& error);

        bool is_me(uint8_t controller_id);
        bool ready_to_send_input();
        void send_input();

        void handle_error(const boost::system::error_code& error);

    public:
        boost::asio::ip::tcp::socket socket;

    private:
        // Initialized in constructor
        server& my_server;
        uint32_t id;

        // Read from client
        std::vector<wchar_t> name;
        std::vector<CONTROL> controllers;

        // Determined by server
        uint8_t player_start;
        uint8_t total_players;

        // Temp input variables
        uint8_t one_byte;
        uint16_t two_bytes;
        std::vector<BUTTONS> in_buttons;
        std::vector<wchar_t> text;

        // Output
        std::vector<std::list<BUTTONS> > out_buttons;
        std::list<packet> out_buffer;
        packet output;
};
