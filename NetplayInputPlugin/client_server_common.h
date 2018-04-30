#pragma once

#include "stdafx.h"

const static uint32_t PROTOCOL_VERSION = 26;
const static uint8_t  MAX_PLAYERS      =  4;

enum PACKET_TYPE : uint8_t {
    VERSION,
    JOIN,
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
