#pragma once

#include "stdafx.h"

#include "packet.h"

class connection: public std::enable_shared_from_this<connection> {
public:
    connection(std::shared_ptr<asio::ip::tcp::socket> socket);
    void read(std::function<void(packet& p)> read_handler);
    void send(const packet& p, bool flush = true);
    void flush();

protected:
    std::shared_ptr<asio::ip::tcp::socket> socket;

    virtual void handle_error(const asio::error_code& error) = 0;

private:
    uint8_t packet_size_buffer[2];

    std::vector<uint8_t> output_buffer;
    bool is_writing = false;
};
