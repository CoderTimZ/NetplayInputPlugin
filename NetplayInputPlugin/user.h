#pragma once

#include "controller.h"
#include "controller_map.h"

#include <string>
#include <cstdint>

struct user {
    uint32_t id;
    std::string name;
    int32_t latency = -1;
    controller::CONTROL controllers[4];
    controller_map control_map;
};