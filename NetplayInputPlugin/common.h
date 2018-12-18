#pragma once

#include "stdafx.h"

const static uint32_t PROTOCOL_VERSION = 33;

enum PACKET_TYPE : uint8_t {
    VERSION,
    JOIN,
    ACCEPT,
    PATH,
    PING,
    PONG,
    QUIT,
    NAME,
    LATENCY,
    MESSAGE,
    LAG,
    AUTOLAG,
    CONTROLLERS,
    START,
    GOLF,
    CONTROLLER_MAP,
    INPUT_DATA,
    INPUT_FILL,
    FRAME,
    SYNC_REQ,
    SYNC_RES
};

enum MESSAGE_TYPE : int32_t {
    ERROR_MESSAGE  = -2,
    STATUS_MESSAGE = -1
};

double timestamp();
std::string endpoint_to_string(const asio::ip::tcp::endpoint& endpoint);
void log(const std::string& message);
void log(std::ostream& stream, const std::string& message);
