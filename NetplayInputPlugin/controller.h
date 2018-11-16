#pragma once

#include "stdafx.h"

enum PAK_TYPE : uint32_t {
    NONE	 = 1,
    MEM      = 2,
    RUMBLE   = 3,
    TRANSFER = 4
};

typedef struct {
    uint32_t present = 0;
    uint32_t raw_data = 0;
    uint32_t plugin = PAK_TYPE::NONE;
} controller;

typedef union {
    uint32_t value;
    struct {
        unsigned d_r : 1;
        unsigned d_l : 1;
        unsigned d_d : 1;
        unsigned d_u : 1;
        unsigned start : 1;
        unsigned z : 1;
        unsigned b : 1;
        unsigned a : 1;

        unsigned c_r : 1;
        unsigned c_l : 1;
        unsigned c_d : 1;
        unsigned c_u : 1;
        unsigned r : 1;
        unsigned l : 1;
        unsigned reserved1 : 1;
        unsigned reserved2 : 1;

        signed joy_y : 8;
        signed joy_x : 8;
    };
} input;
