#pragma once

#include "stdafx.h"

#include "packet.h"

class connection: public std::enable_shared_from_this<connection> {
public:
    connection(std::shared_ptr<asio::io_service> io_s);
    asio::ip::tcp::socket& get_socket();
    void receive(std::function<void(packet&)> receive_handler);
    void send(const packet& pout, bool flush = true);
    void flush();
    virtual void close();

    std::shared_ptr<asio::io_service> io_s;
    asio::ip::tcp::socket socket;

    virtual void handle_error(const asio::error_code& error);

protected:
    packet pout;

private:
    void receive_varint(std::function<void(packet&)> receive_handler);

    packet read_buffer;
    packet write_buffer[2];
};
