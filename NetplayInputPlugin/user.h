#pragma once

#include "stdafx.h"

#include "common.h"
#include "connection.h"
#include "server.h"
#include "room.h"
#include "packet.h"

class user : public connection {
    public:
        user(server* server);
        virtual void on_receive(packet& packet, bool reliable);
        virtual void on_error(const std::error_code& error);
        void set_room(room* room);
        double get_median_latency() const;
        double get_input_rate();
        void write_input_from(user* from);
        void flush_input();
        bool set_input_authority(application authority, application initiator = HOST);
        void set_lag(uint8_t lag, user* source);
        void send_keepalive();
        void send_protocol_version();
        void send_accept();
        void send_join(const user_info& name);
        void send_save_info(uint32_t id, const std::array<save_info, 5>& saves);
        void send_save_sync(const std::array<save_info, 5>& saves);

        void send_name(uint32_t id, const std::string& name);
        void send_ping();
        void send_quit(uint32_t id);
        void send_message(uint32_t id, const std::string& message);
        void send_info(const std::string& message);
        void send_error(const std::string& message);
        void send_start_game();

    private:
        void record_input_timestamp();

        server* my_server;
        room* my_room = nullptr;
        uint32_t id = 0xFFFFFFFF;
        std::string address;
        user_info info;
        std::list<double> input_timestamps;
        std::list<double> latency_history;
        double last_pong = 0;
        input_data hia_input;
        packet udp_input_buffer;

        friend class room;
        friend class server;
};
