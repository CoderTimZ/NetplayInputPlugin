#pragma once

#include "stdafx.h"

#include "Controller 1.0.h"
#include "controller_map.h"

struct user_data {
    uint32_t id;
    std::string name;
    double latency = NAN;
    CONTROL controllers[MAX_PLAYERS];
    controller_map controller_map;
};