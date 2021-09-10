#pragma once

#include "stdafx.h"

#include "common.h"
#include "packet.h"
#include "room.h"

class server {
public:
    server(asio::io_service& service, bool multiroom);

    uint16_t open(uint16_t port);
    void close();
    void on_user_join(user* user, std::string room);
    void on_user_quit(user* user);
    void on_room_close(room* room);
    void log_room_list();

private:
    void accept();
    void read();
    void on_tick();
    std::string get_random_room_id();
    
    asio::io_service* service;
    bool multiroom;
    asio::ip::tcp::acceptor acceptor;
    asio::ip::udp::socket udp_socket;
    asio::steady_timer timer;
    std::map<std::string, std::shared_ptr<room>, ci_less> rooms;
    std::unordered_map<user*, std::shared_ptr<user>> users;
    uint32_t tick_count = 0;

    friend room;
    friend user;
};
