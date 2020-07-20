#pragma once

#include "stdafx.h"

#include "packet.h"

class connection: public std::enable_shared_from_this<connection> {
public:
    connection(asio::io_service& io_service);
    bool is_open();
    virtual void close(const std::error_code& error = std::error_code());
    void close_udp();
    void send(const packet& packet, bool reliable = true, bool flush = true);
    void flush();

protected:
    virtual void on_receive(packet& packet, bool reliable) = 0;
    virtual void on_error(const std::error_code& error) = 0;

    void receive_tcp_packet_size(std::function<void(size_t)> handler, size_t size = 0, int shift = 0);
    void receive_tcp_packet();
    void receive_udp_packet();

    std::shared_ptr<asio::ip::tcp::socket> tcp_socket;
    std::shared_ptr<asio::ip::udp::socket> udp_socket;

    packet output_buffer;
    bool flushing = false;
    bool can_send_udp = false;
    bool can_recv_udp = false;
};