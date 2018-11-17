#pragma once

#include "stdafx.h"

#include "connection.h"
#include "packet.h"
#include "Controller 1.0.h"
#include "controller_map.h"
#include "blocking_queue.h"
#include "common.h"
#include "client_dialog.h"
#include "server.h"
#include "user_data.h"

struct input_id {
    uint32_t user_id;
    uint8_t src_port;
    uint8_t dst_port;

    bool operator==(const input_id& other) const {
        return user_id == other.user_id && src_port == other.src_port && dst_port == other.dst_port;
    }
};

namespace std {
    template <>
    struct hash<input_id> {
        std::size_t operator()(const input_id& k) const {
            return (k.user_id << 4) ^ (k.src_port << 2) ^ (k.dst_port);
        }
    };
}

class client: public connection {
    public:
        client(std::shared_ptr<asio::io_service> io_s, std::shared_ptr<client_dialog> my_dialog);
        ~client();

        std::shared_ptr<client> shared_from_this() {
            return std::static_pointer_cast<client>(connection::shared_from_this());
        }

        void load_public_server_list();
        void ping_public_server_list();
        std::string get_name();
        void set_name(const std::string& name);
        void set_src_controllers(CONTROL controllers[MAX_PLAYERS]);
        void process_input(BUTTONS src_input[MAX_PLAYERS]);
        void get_input(int port, BUTTONS* input);
        void set_dst_controllers(CONTROL dst_controllers[MAX_PLAYERS]);
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
        uint32_t my_id = 0;
        std::map<uint32_t, user_data> users;
        uint8_t lag = 0;
        int current_lag = 0;
        std::map<std::string, double> public_servers;

        CONTROL* dst_controllers;
        std::array<CONTROL, MAX_PLAYERS> src_controllers;
        std::unordered_map<input_id, blocking_queue<BUTTONS>> input_queues;

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
        void map_src_to_dst();
        void send_join(const std::string& room);
        void send_name();
        void send_controllers();
        void send_message(const std::string& message);
        void send_start_game();
        void send_lag(uint8_t lag);
        void send_input(BUTTONS input[MAX_PLAYERS]);
        void send_autolag(int8_t value = -1);
        void send_frame();
        void send_controller_map(controller_map map);
        void handle_input(uint32_t user_id, BUTTONS input[MAX_PLAYERS]);
};
