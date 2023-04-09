#pragma once

#include "stdafx.h"

#include "common.h"
#include "connection.h"
#include "server.h"
#include "room.h"
#include "packet.h"

class user : public connection, public user_info {
    public:
        user(server* server);
        virtual void on_receive(packet& packet, bool udp);
        virtual void on_error(const std::error_code& error);
        void set_room(room* room);
        double get_latency() const;
        void write_input_from(user* from);
        void set_lag(uint8_t lag, user* source);
        void send_keepalive();
        void send_protocol_version();
        void send_accept();
        void send_join(const user_info& info);
        void send_save_info(uint32_t id, const std::array<save_info, 5>& saves);
        void send_save_sync(const std::array<save_info, 5>& saves);
        void send_name(uint32_t id, const std::string& name);
        void send_ping();
        void send_quit(uint32_t id);
        void send_message(uint32_t id, const std::string& message);
        void send_info(const std::string& message);
        void send_error(const std::string& message);
        void send_start_game();
        void send_input_update(uint32_t id, const input_data& input);
        void send_request_authority(uint32_t user_id, uint32_t authority_id);
        void send_delegate_authority(uint32_t user_id, uint32_t authority_id);

    private:
        server* my_server;
        room* my_room = nullptr;
        std::string address;
        user_info info;
        float input_rate = 0;
        std::list<double> latency_history;
        double join_timestamp = INFINITY;

        friend class room;
        friend class server;
};
