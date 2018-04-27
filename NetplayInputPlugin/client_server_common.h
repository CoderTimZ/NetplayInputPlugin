#pragma once

#include <cstdint>

const static uint32_t PROTOCOL_VERSION = 25;
const static uint8_t  MAX_PLAYERS      =  4;
const static uint8_t  DEFAULT_LAG      =  5;

enum PACKET_TYPE : uint8_t {
    VERSION,
    PING,
    PONG,
    QUIT,
    NAME,
    MESSAGE,
    LATENCIES,
    LAG,
    AUTOLAG,
    CONTROLLERS,
    START,
    INPUT_DATA,
    FRAME
};
