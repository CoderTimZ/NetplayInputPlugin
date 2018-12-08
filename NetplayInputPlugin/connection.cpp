#include "stdafx.h"

#include "connection.h"

using namespace std;
using namespace asio;

connection::connection(shared_ptr<io_service> io_s) : io_s(io_s), socket(*io_s) { }

ip::tcp::socket& connection::get_socket() {
    return socket;
}

void connection::receive(std::function<void(packet&)> receive_handler) {
    read_buffer.reset();
    auto self(shared_from_this());
    receive_varint([=](packet& pin) {
        read_buffer.reset(pin.read_var<size_t>());
        async_read(socket, buffer(read_buffer), [=](const error_code& error, size_t transferred) {
            if (error) return self->handle_error(error);
            receive_handler(read_buffer);
        });
    });
}

void connection::receive_varint(std::function<void(packet&)> receive_handler) {
    read_buffer.resize(read_buffer.size() + 1);
    auto self(shared_from_this());
    async_read(socket, buffer(&read_buffer.back(), 1), [=](const error_code& error, size_t transferred) {
        if (error) return self->handle_error(error);
        if (read_buffer.back() & 0x80) return self->receive_varint(receive_handler);
        receive_handler(read_buffer);
    });
}

void connection::send(const packet& pout, bool f) {
    if (!socket.is_open()) return;
    write_buffer[0] << pout;
    if (f) flush();
}

void connection::flush() {
    if (!write_buffer[0].empty() && write_buffer[1].empty()) {
        write_buffer[0].swap(write_buffer[1]);
        auto self(shared_from_this());
        async_write(socket, buffer(write_buffer[1]), [self](const error_code& error, size_t transferred) {
            self->write_buffer[1].reset();
            if (error) return self->handle_error(error);
            self->flush();
        });
    }
}

void connection::close() {
    error_code error;
    socket.shutdown(ip::tcp::socket::shutdown_both, error);
    socket.close(error);
}

void connection::handle_error(const error_code& error) {
    close();
}
