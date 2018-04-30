#pragma once

#include "stdafx.h"

#include "controller.h"
#include "controller_map.h"

struct user {
    uint32_t id;
    std::string name;
    int32_t latency = -1;
    controller controllers[4];
    controller_map control_map;
};