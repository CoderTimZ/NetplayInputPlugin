#include "stdafx.h"

#include "connection.h"
#include "common.h"

using namespace std;
using namespace asio;

connection::connection(asio::io_service& service) :
    udp_resolver(service), tcp_socket(make_shared<ip::tcp::socket>(service)), udp_socket(make_shared<ip::udp::socket>(service)) { }

bool connection::is_open() {
    return tcp_socket && tcp_socket->is_open();
}

void connection::close(const error_code& error) {
    if (tcp_socket && tcp_socket->is_open()) {
        error_code ec;
        tcp_socket->shutdown(ip::tcp::socket::shutdown_both, ec);
        tcp_socket->close(ec);
    }
    tcp_socket.reset();
    tcp_output_buffer.clear();

    close_udp();
    on_error(error);
}

void connection::close_udp() {
    if (udp_socket && udp_socket->is_open()) {
        error_code ec;
        udp_socket->shutdown(ip::udp::socket::shutdown_both, ec);
        udp_socket->close(ec);
    }
    udp_socket.reset();
    udp_output_buffer.clear();
    udp_established = false;
}

void connection::send(const packet& packet, bool flush) {
    if (!tcp_socket || !tcp_socket->is_open()) return;

    tcp_output_buffer << packet;

    if (flush) {
        this->flush();
    }
}

void connection::send_udp(const packet& packet, bool flush) {
    if (!udp_socket || !udp_socket->is_open()) return;

    size_t size = (packet.size() < 0x80 ? 1 : (packet.size() < 0x4000 ? 2 : 3)) + packet.size();
    if (size > MAX_UDP_SIZE) return;

    if (udp_output_buffer.size() + size > MAX_UDP_SIZE) {
        this->flush_udp();
    }

    udp_output_buffer << packet;

    if (flush) {
        this->flush_udp();
    }
}

void connection::flush() {
    if (!tcp_socket || !tcp_socket->is_open()) return;

    if (tcp_output_buffer.empty()) return;
    if (flushing) return;

    auto p(make_shared<packet>());
    p->swap(tcp_output_buffer);
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

void connection::flush_udp() {
    if (!udp_socket || !udp_socket->is_open()) return;

    if (udp_output_buffer.empty()) return;

    error_code error;
    udp_socket->send(buffer(udp_output_buffer), 0, error);
    udp_output_buffer.clear();
    if (error) close_udp();
}

void connection::flush_all() {
    flush();
    flush_udp();
}

void connection::query_udp_port(std::function<void()> handler) {
    auto handled = make_shared<bool>(false);
    auto handle = [handler, handled]() {
        if (!*handled) {
            *handled = true;
            handler();
        }
    };

    if (!tcp_socket || !udp_socket) {
        external_udp_port = 0;
        return handle();
    }

    external_udp_port = udp_socket->local_endpoint().port();

    auto local_addr = tcp_socket->local_endpoint().address();
    auto remote_addr = tcp_socket->remote_endpoint().address();
    if (!is_private_address(local_addr) || is_private_address(remote_addr)) {
        return handle();
    }

    auto u(udp_socket);
    auto s(weak_from_this());
    udp_resolver.async_resolve(udp_socket->local_endpoint().protocol(), "query.play64.com", "6400", [=](const auto& error, auto iterator) {
        if (s.expired() || u != udp_socket) return handle();
        if (error) return handle();
        auto p(make_shared<packet>());
        *p << EXTERNAL_ADDRESS;
        udp_socket->async_send_to(buffer(*p), iterator->endpoint(), [=](const error_code& error, size_t transferred) {
            if (s.expired() || u != udp_socket) return handle();
            p->reset();
            if (error) return handle();

            auto timer = make_shared<asio::steady_timer>(udp_resolver.get_executor());
            timer->expires_after(std::chrono::seconds(1));

            udp_socket->async_wait(ip::udp::socket::wait_read, [=](const error_code& error) {
                if (s.expired() || u != udp_socket) return handle();
                timer->cancel();
                if (error) return handle();
                error_code ec;
                p->resize(udp_socket->available(ec));
                if (ec) return handle();
                p->resize(udp_socket->receive(buffer(*p), 0, ec));
                if (ec) return handle();
                if (p->available() < sizeof(query_type) || p->read<query_type>() != EXTERNAL_ADDRESS) return handle();
                if (p->available() < sizeof(uint16_t)) return handle();
                external_udp_port = p->read<uint16_t>();
                return handle();
            });

            timer->async_wait([this, s, u, timer, handle](const asio::error_code& error) {
                if (s.expired() || u != udp_socket) return handle();
                if (error) return handle();
                udp_socket->close();
            });
        });
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
        if (size == 0) return receive_tcp_packet();
        if (size > packet::MAX_SIZE) {
            log(cerr, "packet too large");
            return close();
        }
        auto p(make_shared<packet>(size));
        auto t(tcp_socket);
        auto s(weak_from_this());
        async_read(*t, buffer(*p), [=](const error_code& error, size_t transferred) {
            if (s.expired() || t != tcp_socket) return;
            if (error) return close(error);
            try {
                on_receive(*p, false);
            } catch (const exception& e) {
                log(cerr, e.what());
                return close();
            } catch (const error_code& e) {
                return close(e);
            }
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
        if (error) return close_udp();
        error_code ec;
        while (size_t size = udp_socket->available(ec)) {
            if (ec) return close_udp();
            packet buf(size);
            ip::udp::endpoint ep;
            size = udp_socket->receive_from(buffer(buf), ep, 0, ec);
            if (ec) return close_udp();
            if (ep != udp_socket->remote_endpoint()) continue;
            buf.resize(size);
            while (buf.available()) {
                try {
                    packet p;
                    buf.read(p);
                    if (p.empty()) continue;
                    on_receive(p, true);
                } catch (const exception&) {
                    return close_udp();
                } catch (const error_code&) {
                    return close_udp();
                }
            }
        }
        receive_udp_packet();
    });
}