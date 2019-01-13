#include "stdafx.h"

#include "connection.h"

using namespace std;
using namespace asio;

connection::connection(shared_ptr<io_service> io_s) : io_s(io_s), socket(*io_s) { }

ip::tcp::socket& connection::get_socket() {
    return socket;
}

void connection::receive(function<void(packet&, const error_code& error)> handler) {
    read_buffer.reset();
    auto self(shared_from_this());
    receive_varint([=](packet& pin, const error_code& error) {
        self->read_buffer.reset(pin.read_var<size_t>());
        async_read(self->socket, buffer(self->read_buffer), [=](const error_code& error, size_t transferred) {
            handler(self->read_buffer, error);
        });
    });
}

void connection::receive_varint(function<void(packet&, const error_code& error)> handler) {
    read_buffer.resize(read_buffer.size() + 1);
    auto self(shared_from_this());
    async_read(socket, buffer(&read_buffer.back(), 1), [=](const error_code& error, size_t transferred) {
        if (!error) {
            if (self->read_buffer.back() & 0x80) return self->receive_varint(handler);
        }
        handler(self->read_buffer, error);
    });
}

void connection::send(const packet& pout, function<void(const error_code&)> handler, bool flush) {
    if (!socket.is_open()) return;
    write_buffer[0] << pout;
    if (flush) this->flush(handler);
}

void connection::flush(function<void(const error_code&)> handler) {
    if (!write_buffer[0].empty() && write_buffer[1].empty()) {
        write_buffer[0].swap(write_buffer[1]);
        auto self(shared_from_this());
        async_write(socket, buffer(write_buffer[1]), [self, handler](const error_code& error, size_t transferred) {
            self->write_buffer[1].reset();
            if (error) return handler(error);
            self->flush(handler);
        });
    }
}

void connection::close() {
    error_code error;
    socket.shutdown(ip::tcp::socket::shutdown_both, error);
    socket.close(error);
}
