#pragma once

#include <stdint.h>

const static uint16_t MY_PROTOCOL_VERSION = 0x0020;
const static uint8_t  MAX_PLAYERS         = 4;
const static uint8_t  DEFAULT_LAG         = 5;

// Client <--> Server
const static uint8_t PROTOCOL_VERSION =  0;
const static uint8_t KEEP_ALIVE       =  1;
const static uint8_t PING             =  2;
const static uint8_t NAME             =  3;
const static uint8_t CONTROLLERS      =  4;
const static uint8_t LEFT             =  5;
const static uint8_t CHAT             =  6;
const static uint8_t CHAT_TYPING      =  7;
const static uint8_t CHAT_ENTERED     =  8;
const static uint8_t LAG              =  9;
const static uint8_t START_GAME       = 10;
const static uint8_t PLAYER_RANGE     = 11;
const static uint8_t INPUT_DATA       = 12;
