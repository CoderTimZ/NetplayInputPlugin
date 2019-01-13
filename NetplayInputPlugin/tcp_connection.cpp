#include "stdafx.h"

#include "tcp_connection.h"
#include "common.h"

using namespace std;
using namespace asio;

tcp_connection::tcp_connection(shared_ptr<io_service> io_s) : io_s(io_s), socket(*io_s) { }

ip::tcp::socket& tcp_connection::get_socket() {
    return socket;
}

bool tcp_connection::is_open() {
    return socket.is_open();
}

void tcp_connection::close() {
    if (is_open()) {
        error_code error;
        socket.shutdown(ip::tcp::socket::shutdown_both, error);
        socket.close(error);
    }
}

void tcp_connection::receive(function<void(packet&, const error_code& error)> handler) {
    read_buffer.reset();
    auto self(shared_from_this());
    receive_varint([=](packet& pin, const error_code& error) {
        if (error) {
            if (handler) handler(self->read_buffer, error);
        } else {
            self->read_buffer.reset(pin.read_var<size_t>());
            async_read(self->socket, buffer(self->read_buffer), [=](const error_code& error, size_t transferred) {
                if (handler) handler(self->read_buffer, error);
            });
        }
    });
}

void tcp_connection::receive_varint(function<void(packet&, const error_code& error)> handler) {
    read_buffer.resize(read_buffer.size() + 1);
    auto self(shared_from_this());
    async_read(socket, buffer(&read_buffer.back(), 1), [=](const error_code& error, size_t transferred) {
        if (error) {
            if (handler) handler(self->read_buffer, error);
        } else if (self->read_buffer.back() & 0x80) {
            return self->receive_varint(handler);
        } else if (handler) {
            handler(self->read_buffer, error_code());
        }
    });
}

void tcp_connection::send(const packet& pout, function<void(const error_code&)> handler, bool flush) {
    if (!socket.is_open()) {
        if (handler) handler(error::not_connected);
    } else {
        write_buffer[0] << pout;
        if (flush) {
            this->flush(handler);
        } else if (handler) {
            handler(error_code());
        }
    }
}

void tcp_connection::flush(function<void(const error_code&)> handler) {
    if (!socket.is_open()) {
        if (handler) handler(error::not_connected);
    } else if (!write_buffer[0].empty() && write_buffer[1].empty()) {
        write_buffer[0].swap(write_buffer[1]);
        auto self(shared_from_this());
        async_write(socket, buffer(write_buffer[1]), [self, handler](const error_code& error, size_t transferred) {
            self->write_buffer[1].reset();
            if (error) {
                if (handler) return handler(error);
            }
            self->flush(handler);
        });
    } else if (handler) {
        handler(error_code());
    }
}

string tcp_connection::get_address() {
    return is_open() ? endpoint_to_string(socket.remote_endpoint()) : "N/A";
}