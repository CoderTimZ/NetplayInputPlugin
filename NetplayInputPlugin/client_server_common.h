#pragma once

#include <cstdint>

const static uint32_t PROTOCOL_VERSION = 24;
const static uint8_t  MAX_PLAYERS      =  4;
const static uint8_t  DEFAULT_LAG      =  5;

// Client <--> Server
const static uint8_t VERSION      = 0x00;
const static uint8_t PING         = 0x01;
const static uint8_t PONG         = 0x02;
const static uint8_t NAME         = 0x03;
const static uint8_t CONTROLLERS  = 0x04;
const static uint8_t QUIT         = 0x05;
const static uint8_t MESSAGE      = 0x06;
const static uint8_t LAG          = 0x07;
const static uint8_t START        = 0x08;
const static uint8_t PLAYER_RANGE = 0x09;
const static uint8_t INPUT_DATA   = 0x0a;
const static uint8_t LATENCIES    = 0x0b;
const static uint8_t AUTOLAG      = 0x0c;