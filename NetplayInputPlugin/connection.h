#pragma once

#include "stdafx.h"

#include "packet.h"

class connection: public std::enable_shared_from_this<connection> {
public:
    connection(std::shared_ptr<asio::io_service> io_s);
    asio::ip::tcp::socket& get_socket();
    void receive(std::function<void(packet&, const asio::error_code&)> handler);
    void send(const packet& pout, std::function<void(const asio::error_code&)> handler, bool flush = true);
    void flush(std::function<void(const asio::error_code&)> handler);
    virtual void close();

    std::shared_ptr<asio::io_service> io_s;
    asio::ip::tcp::socket socket;

private:
    void receive_varint(std::function<void(packet&, const asio::error_code&)> handler);

    packet read_buffer;
    packet write_buffer[2];
};
