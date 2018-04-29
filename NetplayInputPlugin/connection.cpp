#include "stdafx.h"

#include "connection.h"

using namespace std;
using namespace asio;

connection::connection(std::shared_ptr<asio::io_service> io_s) : io_s(io_s), socket(*io_s) { }

void connection::read(std::function<void(packet& p)> read_handler) {
    auto self(shared_from_this());
    async_read(socket, buffer(packet_size_buffer, sizeof packet_size_buffer), [=](const error_code& error, size_t transferred) {
        if (error) return self->handle_error(error);
        uint16_t packet_size = (self->packet_size_buffer[0] << 8) | self->packet_size_buffer[1];
        auto p = make_shared<packet>(packet_size);
        if (packet_size == 0) return read_handler(*p);
        async_read(socket, buffer(p->data()), [=](const error_code& error, size_t transferred) {
            if (error) return self->handle_error(error);
            read_handler(*p);
        });
    });
}

void connection::send(const packet& p, bool f) {
    assert(p.size() <= 0xFFFF);
    output_buffer.push_back(p.size() >> 8);
    output_buffer.push_back(p.size() & 0xFF);
    output_buffer.insert(output_buffer.end(), p.data().begin(), p.data().end());
    if (f) flush();
}

void connection::flush() {
    if (is_writing || output_buffer.empty()) return;

    auto b = make_shared<vector<uint8_t>>(output_buffer);
    output_buffer.clear();
    is_writing = true;
    auto self(shared_from_this());
    async_write(socket, buffer(*b), [self, b](const error_code& error, size_t transferred) {
        self->is_writing = false;
        if (error) return self->handle_error(error);
        self->flush();
    });
}
