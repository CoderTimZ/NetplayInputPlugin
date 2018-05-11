#pragma once

#include "stdafx.h"

#include "common.h"

struct controller_map {
    void erase(int local_port) {
        for (size_t i = 0; i < MAX_PLAYERS; ++i) {
            if (netplay_to_local[i] == local_port) {
                netplay_to_local[i] = -1;
            }
        }
    }

    void insert(int local_port, int netplay_port) {
        erase(local_port);

        if (0 <= netplay_port && netplay_port < MAX_PLAYERS) {
            netplay_to_local[netplay_port] = local_port;
        }
    }

    int to_local(int netplay_port) const {
        if (0 <= netplay_port && netplay_port < MAX_PLAYERS) {
            return netplay_to_local[netplay_port];
        } else {
            return -1;
        }
    }

    int count() const {
        int count = 0;
        for (size_t i = 0; i < MAX_PLAYERS; ++i) {
            if (netplay_to_local[i] >= 0) {
                count++;
            }
        }
        return count;
    }

    int8_t netplay_to_local[MAX_PLAYERS] = { -1, -1, -1, -1 };
};
