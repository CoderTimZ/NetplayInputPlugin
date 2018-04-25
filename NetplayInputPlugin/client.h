#pragma once

#include <cstdint>
#include <vector>
#include <list>
#include <map>
#include <string>
#include <boost/asio.hpp>

#include "packet.h"
#include "Controller 1.0.h"
#include "blocking_queue.h"
#include "client_server_common.h"
#include "client_dialog.h"
#include "server.h"

class client {
    public:
        client(client_dialog* my_dialog);
        ~client();

        std::string get_name();
        void set_name(const std::string& name);
        bool plugged_in(uint8_t index);
        void set_local_controllers(CONTROL controllers[MAX_PLAYERS]);
        void process_input(std::vector<BUTTONS>& input);
        void set_netplay_controllers(CONTROL netplay_controllers[MAX_PLAYERS]);
        void wait_for_game_to_start();

    private:
        boost::asio::io_service io_s;
        boost::asio::io_service::work work;
        boost::asio::ip::tcp::resolver resolver;
        boost::asio::ip::tcp::socket socket;
        std::thread thread;
        std::mutex mut;
        std::condition_variable game_started_condition;

        bool connected;
        std::list<packet> output_queue;
        packet output_buffer;

        bool game_started;
        bool online;
        int current_lag;
        uint32_t frame;
        bool golf;

        std::string name;
        std::map<uint32_t, std::string> names;
        std::map<uint32_t, uint32_t> latencies;
        uint8_t player_index;
        uint8_t player_count;
        uint8_t lag;

        CONTROL* netplay_controllers;
        std::vector<CONTROL> local_controllers;
        std::list<std::vector<BUTTONS>> remote_input;
        std::list<std::vector<BUTTONS>> local_input;
        blocking_queue<std::vector<BUTTONS>> queue;

        std::shared_ptr<client_dialog> my_dialog;
        std::shared_ptr<server> my_server;

        uint8_t get_total_count();
        void enqueue_if_ready();
        void stop();
        void handle_error(const boost::system::error_code& error, bool lost_connection);
        void next_packet();
        void send(const packet& p);
        void flush();
        
        uint8_t get_remote_count();
        std::vector<CONTROL> get_local_controllers(); 
        void process_command(std::string command);
        void set_lag(uint8_t lag, bool show_message = true);
        void game_has_started();
        void update_netplay_controllers(const std::array<CONTROL, MAX_PLAYERS>& netplay_controllers);
        void set_player_index(uint8_t player_index);
        void set_player_count(uint8_t player_count);
        void set_user_name(uint32_t id, const std::string& name);
        void set_user_latency(uint32_t id, uint32_t latency);
        void remove_user(uint32_t id);
        void chat_received(int32_t id, const std::string& message);
        void incoming_remote_input(const std::vector<BUTTONS>& input);
        void client_error();
        const std::map<uint32_t, std::string>& get_names() const;
        const std::map<uint32_t, uint32_t>& get_latencies() const;
        void connect(const std::string& host, uint16_t port);
        void send_protocol_version();
        void send_name(const std::string& name);
        void send_controllers(const std::vector<CONTROL>& controllers);
        void send_chat(const std::string& message);
        void send_start_game();
        void send_lag(uint8_t lag);
        void send_input(uint32_t frame, const std::vector<BUTTONS>& input);
        void send_auto_lag();
        bool is_connected();
};
