#include "stdafx.h"

#include "connection.h"

using namespace std;
using namespace asio;

connection::connection(asio::io_service& service) :
    tcp_socket(make_shared<ip::tcp::socket>(service)), udp_socket(make_shared<ip::udp::socket>(service)) { }

bool connection::is_open() {
    return tcp_socket && tcp_socket->is_open() &&
           udp_socket && udp_socket->is_open();
}

void connection::close(const error_code& error) {
    if (tcp_socket && tcp_socket->is_open()) {
        error_code ec;
        tcp_socket->shutdown(ip::tcp::socket::shutdown_both, ec);
        tcp_socket->close(ec);
    }
    if (udp_socket && udp_socket->is_open()) {
        error_code ec;
        udp_socket->shutdown(ip::udp::socket::shutdown_both, ec);
        udp_socket->close(ec);
    }
    tcp_socket.reset();
    udp_socket.reset();
    output_buffer.clear();
    if (error) {
        on_error(error);
    }
}

void connection::send(const packet& packet, bool reliable, bool flush) {
    if (reliable) {
        if (!tcp_socket || !tcp_socket->is_open()) return;
        output_buffer << packet;
        if (flush) {
            this->flush();
        }
    } else {
        if (!udp_socket || !udp_socket->is_open()) return;
        error_code error;
        udp_socket->send(buffer(packet), 0, error);
        if (error) {
            close(error);
        }
    }
}

void connection::flush() {
    if (!tcp_socket || !tcp_socket->is_open()) return;
    if (output_buffer.empty()) return;
    if (flushing) return;

    auto p(make_shared<packet>());
    p->swap(output_buffer);
    flushing = true;

    auto t(tcp_socket);
    auto s(weak_from_this());
    async_write(*t, buffer(*p), [this, t, p, s](const error_code& error, size_t transferred) {
        if (s.expired() || t != tcp_socket) return;
        if (error) return close(error);
        flushing = false;
        flush();
    });
}

void connection::receive_tcp_packet_size(function<void(size_t)> handler, size_t value, int count) {
    if (!tcp_socket || !tcp_socket->is_open()) return;
    auto b = make_shared<uint8_t>();
    auto t(tcp_socket);
    auto s(weak_from_this());
    async_read(*t, buffer(b.get(), 1), [=](const error_code& error, size_t transferred) {
        if (s.expired() || t != tcp_socket) return;
        if (error) return close(error);
        auto size = value | (static_cast<size_t>(*b & 0b01111111) << (count * 7));
        if (*b & 0b10000000) {
            receive_tcp_packet_size(handler, size, count + 1);
        } else {
            handler(size);
        }
    });
}

void connection::receive_tcp_packet() {
    receive_tcp_packet_size([=](size_t size) {
        if (!tcp_socket || !tcp_socket->is_open()) return;
        auto p(make_shared<packet>(size));
        auto t(tcp_socket);
        auto s(weak_from_this());
        async_read(*t, buffer(*p), [=](const error_code& error, size_t transferred) {
            if (s.expired() || t != tcp_socket) return;
            if (error) return close(error);
            on_receive(*p, true);
            receive_tcp_packet();
        });
    });
}

void connection::receive_udp_packet() {
    if (!udp_socket || !udp_socket->is_open()) return;
    auto u(udp_socket);
    auto s(weak_from_this());
    u->async_wait(ip::udp::socket::wait_read, [=](const error_code& error) {
        if (s.expired() || u != udp_socket) return;
        if (error) return close(error);
        error_code ec;
        while (size_t size = udp_socket->available(ec)) {
            if (ec) return close(ec);
            packet p(size);
            size = udp_socket->receive(buffer(p), 0, ec);
            if (ec) return close(ec);
            p.resize(size);
            if (!p.empty()) on_receive(p, false);
        }
        receive_udp_packet();
    });
}