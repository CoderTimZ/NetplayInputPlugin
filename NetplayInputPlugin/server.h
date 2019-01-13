#pragma once

#include "stdafx.h"

#include "common.h"
#include "packet.h"
#include "room.h"

class server : public std::enable_shared_from_this<server> {
public:
    server(std::shared_ptr<asio::io_service> io_s, bool multiroom);

    uint16_t open(uint16_t port);
    void close();
    void on_user_join(std::shared_ptr<user> user, std::string room);
    void on_room_close(std::shared_ptr<room> room);
private:
    void accept();
    void on_tick();
    std::string get_random_room_id();
    
    std::shared_ptr<asio::io_service> io_s;
    bool multiroom;
    asio::ip::tcp::acceptor acceptor;
    asio::steady_timer timer;
    std::unordered_map<std::string, std::shared_ptr<room>> rooms;

    friend room;
};
