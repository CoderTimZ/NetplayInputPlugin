#pragma once

#include "stdafx.h"

#include "connection.h"
#include "packet.h"
#include "Controller 1.0.h"
#include "controller_map.h"
#include "blocking_queue.h"
#include "client_server_common.h"
#include "client_dialog.h"
#include "server.h"
#include "user_data.h"

class client: public connection {
    public:
        client(std::shared_ptr<asio::io_service> io_s, std::shared_ptr<client_dialog> my_dialog);
        ~client();

        std::shared_ptr<client> shared_from_this() {
            return std::static_pointer_cast<client>(connection::shared_from_this());
        }

        std::string get_name();
        void set_name(const std::string& name);
        void set_local_controllers(CONTROL controllers[MAX_PLAYERS]);
        void process_input(BUTTONS local_input[MAX_PLAYERS]);
        void get_input(int port, BUTTONS* input);
        void set_netplay_controllers(CONTROL netplay_controllers[MAX_PLAYERS]);
        void wait_until_start();
        void post_close();

    private:
        asio::io_service::work work;
        asio::ip::tcp::resolver resolver;
        std::thread thread;
        std::mutex mut;
        bool started = false;
        std::condition_variable start_condition;
        uint32_t frame;
        bool golf;

        std::string host;
        uint16_t port;
        std::string path;
        std::string name;
        std::map<uint32_t, user_data> users;
        uint8_t lag = 0;

        CONTROL* netplay_controllers;
        std::array<CONTROL, MAX_PLAYERS> local_controllers;
        controller_map my_controller_map;
        std::array<blocking_queue<BUTTONS>, MAX_PLAYERS> input_queues;

        std::shared_ptr<client_dialog> my_dialog;
        std::shared_ptr<server> my_server;

        uint8_t get_total_count();
        virtual void close();
        void start_game();
        void handle_error(const asio::error_code& error);
        void process_packet();
        void process_message(std::string message);
        void set_lag(uint8_t lag);
        void message_received(int32_t id, const std::string& message);
        void remove_user(uint32_t id);
        void connect(const std::string& host, uint16_t port, const std::string& room);
        void map_local_to_netplay();
        void send_join(const std::string& room);
        void send_name();
        void send_controllers();
        void send_message(const std::string& message);
        void send_start_game();
        void send_lag(uint8_t lag);
        void send_input(uint8_t port, BUTTONS input);
        void send_autolag();
        void send_frame();
};
