#pragma once

#include "stdafx.h"

#include "controller.h"
#include "controller_map.h"

struct user_data {
    uint32_t id;
    std::string name;
    int32_t latency = -1;
    controller controllers[MAX_PLAYERS];
    controller_map control_map;
};