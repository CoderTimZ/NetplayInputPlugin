#pragma once

#include "stdafx.h"

#include "connection.h"
#include "packet.h"
#include "Controller 1.0.h"
#include "controller_map.h"
#include "common.h"
#include "client_dialog.h"
#include "server.h"

static const auto EMPTY_INPUT = std::make_pair(std::array<BUTTONS, 4>{ 0, 0, 0, 0 }, controller_map{ 0 });

struct user_info {
    std::string name;
    double latency = NAN;
    CONTROL controllers[4];
    controller_map control_map;
    std::list<std::pair<std::array<BUTTONS, 4>, controller_map>> input_queue;
    uint32_t sync_id = 0;
    uint32_t sync_frame;

    bool is_player() const {
        return !control_map.empty();
    }
};

class client: public connection {
    public:
        client(std::shared_ptr<asio::io_service> io_service, std::shared_ptr<client_dialog> dialog);
        ~client();

        std::shared_ptr<client> shared_from_this() {
            return std::static_pointer_cast<client>(connection::shared_from_this());
        }

        void load_public_server_list();
        void ping_public_server_list();
        std::string get_name();
        void set_name(const std::string& name);
        void set_src_controllers(CONTROL controllers[4]);
        void process_input(std::array<BUTTONS, 4>& input);
        void set_dst_controllers(CONTROL dst_controllers[4]);
        void wait_until_start();
        void post_close();

    private:
        asio::io_service::work work;
        asio::ip::tcp::resolver resolver;
        std::thread thread;
        bool started = false;
        std::mutex start_mutex;
        std::condition_variable start_condition;
        std::mutex next_input_mutex;
        std::condition_variable next_input_condition;
        std::list<std::array<BUTTONS, 4>> next_input;
        uint32_t frame = 0;
        bool golf = false;
        std::string host;
        uint16_t port;
        std::string path;
        std::string name;
        uint32_t my_id = 0;
        std::map<uint32_t, user_info> users;
        uint8_t lag = 0;
        std::map<std::string, double> public_servers;
        CONTROL* dst_controllers;
        std::array<CONTROL, 4> src_controllers;
        std::list<std::array<BUTTONS, 4>> local_queue;
        std::shared_ptr<client_dialog> my_dialog;
        std::shared_ptr<server> my_server;
        bool syncing = false;
        controller_map golf_map;
        bool frame_limit = true;
#ifdef DEBUG
        std::ofstream input_log;
#endif

        template<typename F> auto run(F&& task);
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
        void on_input();
        void update_user_list();
        void update_frame_limit();
        void set_controller_map(controller_map map);
        void send_join(const std::string& room);
        void send_name();
        void send_controllers();
        void send_message(const std::string& message);
        void send_start_game();
        void send_lag(uint8_t lag);
        void send_input(const std::array<BUTTONS, 4>& input);
        void send_autolag(int8_t value = -1);
        void send_frame();
        void send_controller_map(controller_map map);
};
