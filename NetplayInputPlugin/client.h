#pragma once

#include <cstdint>
#include <vector>
#include <list>
#include <map>
#include <string>
#include <asio.hpp>

#include "packet.h"
#include "Controller 1.0.h"
#include "controller_map.h"
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
        void set_local_controllers(CONTROL controllers[MAX_PLAYERS]);
        void process_input(int port, BUTTONS* input);
        void get_input(int port, BUTTONS* input);
        void set_netplay_controllers(CONTROL netplay_controllers[MAX_PLAYERS]);
        int netplay_to_local(int port);
        void wait_for_game_to_start();
        void frame_complete();

    private:
        asio::io_service io_s;
        asio::io_service::work work;
        asio::ip::tcp::resolver resolver;
        asio::ip::tcp::socket socket;
        std::thread thread;
        std::mutex mut;
        std::condition_variable game_started_condition;

        bool connected;
        std::vector<uint8_t> output_buffer;
        bool writing = false;

        bool game_started;
        std::array<int, MAX_PLAYERS> current_lag;
        uint32_t frame;
        bool golf;

        std::string name;
        std::map<uint32_t, std::string> names;
        std::map<uint32_t, uint32_t> latencies;
        uint8_t lag;

        CONTROL* netplay_controllers;
        std::array<CONTROL, MAX_PLAYERS> local_controllers;
        controller_map my_controller_map;
        std::array<blocking_queue<BUTTONS>, MAX_PLAYERS> input_queues;

        std::shared_ptr<client_dialog> my_dialog;
        std::shared_ptr<server> my_server;

        uint8_t packet_size_buffer[2];

        uint8_t get_total_count();
        void stop();
        bool is_connected();
        void handle_error(const asio::error_code& error, bool lost_connection);
        void process_packet();
        void process_message(std::string message);
        void set_lag(uint8_t lag, bool show_message = true);
        void game_has_started();
        void set_user_name(uint32_t id, const std::string& name);
        void set_user_latency(uint32_t id, uint32_t latency);
        void remove_user(uint32_t id);
        void chat_received(int32_t id, const std::string& message);
        const std::map<uint32_t, std::string>& get_names() const;
        const std::map<uint32_t, uint32_t>& get_latencies() const;
        void connect(const std::string& host, uint16_t port);
        void send_protocol_version();
        void send_name(const std::string& name);
        void send_controllers();
        void send_chat(const std::string& message);
        void send_start_game();
        void send_lag(uint8_t lag);
        void send_input(uint8_t port, BUTTONS* input);
        void send_autolag();
        void send_frame();
        void send(const packet& p, bool flush = true);
        void flush();
};
