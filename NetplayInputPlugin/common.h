#pragma once

#include "stdafx.h"

const static uint32_t PROTOCOL_VERSION = 28;
const static uint8_t  MAX_PLAYERS      =  4;

enum PACKET_TYPE : uint8_t {
    VERSION,
    JOIN,
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
    INPUT_DATA,
    FRAME
};

enum MESSAGE_TYPE : int32_t {
    ERROR_MESSAGE = -2,
    STATUS_MESSAGE = -1
};

double timestamp();