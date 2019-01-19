#pragma once

#include "stdafx.h"

#include "connection.h"
#include "packet.h"

class tcp_connection: public connection {
public:
    tcp_connection(std::shared_ptr<asio::io_service> io_s);
    asio::ip::tcp::socket& get_socket();
    bool is_open();
    void close();
    void receive(std::function<void(packet&, const std::error_code&)> handler);
    void send(const packet& pout, std::function<void(const std::error_code&)> handler, bool flush = true);
    void flush(std::function<void(const std::error_code&)> handler);
    std::string get_address();

    std::shared_ptr<tcp_connection> shared_from_this() {
        return std::static_pointer_cast<tcp_connection>(connection::shared_from_this());
    }

    std::shared_ptr<asio::io_service> io_s;
    asio::ip::tcp::socket socket;

private:
    void receive_varint(std::function<void(packet&, const std::error_code&)> handler);

    packet read_buffer;
    packet write_buffer[2];
    asio::ip::tcp::endpoint remote_endpoint;
};
