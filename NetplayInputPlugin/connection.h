#pragma once

#include "stdafx.h"

#include "packet.h"

class connection: public std::enable_shared_from_this<connection> {
public:
    virtual bool is_open() = 0;
    virtual void close() = 0;
    virtual void receive(std::function<void(packet&, const std::error_code&)> handler) = 0;
    virtual void send(const packet& pout, std::function<void(const std::error_code&)> handler, bool flush = true) = 0;
    virtual void flush(std::function<void(const std::error_code&)> handler) = 0;
    virtual std::string get_address() = 0;
};
