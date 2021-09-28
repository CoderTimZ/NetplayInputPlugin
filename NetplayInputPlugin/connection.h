#pragma once

#include "stdafx.h"

#include "packet.h"

class connection: public std::enable_shared_from_this<connection> {
public:
    connection(asio::io_service& io_service);
    bool is_open();
    virtual void close(const std::error_code& error = std::error_code());
    void close_udp();
    void send(const packet& packet, bool flush = true);
    void send_udp(const packet& packet, bool flush = true);
    void flush();
    void flush_udp();
    void flush_all();

protected:
    virtual void on_receive(packet& packet, bool udp) = 0;
    virtual void on_error(const std::error_code& error) = 0;

    void query_udp_port(std::function<void()> handler);
    void receive_tcp_packet_size(std::function<void(size_t)> handler, size_t size = 0, int shift = 0);
    void receive_tcp_packet();
    void receive_udp_packet();

    asio::ip::udp::resolver udp_resolver;
    std::shared_ptr<asio::ip::tcp::socket> tcp_socket;
    std::shared_ptr<asio::ip::udp::socket> udp_socket;
    uint16_t udp_port = 0;

    packet tcp_output_buffer;
    packet udp_output_buffer;
    bool flushing = false;
    bool udp_established = false;
};