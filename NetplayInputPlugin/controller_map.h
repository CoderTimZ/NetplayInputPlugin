#pragma once

#include "stdafx.h"

#include "client_server_common.h"

class controller_map {
public:
    controller_map() {
        local_to_netplay.fill(-1);
    }

    int to_netplay(int local_port) const {
        if (0 <= local_port && local_port < MAX_PLAYERS) {
            return local_to_netplay[local_port];
        } else {
            return -1;
        }
    }

    int to_local(int netplay_port) const {
        for (int i = 0; i < MAX_PLAYERS; i++) {
            if (local_to_netplay[i] == netplay_port) {
                return i;
            }
        }
        return -1;
    }

    void insert(int local_port, int netplay_port) {
        if (0 <= local_port && local_port < MAX_PLAYERS) {
            local_to_netplay[local_port] = netplay_port;
        }
    }

    int local_count() const {
        int count = 0;
        for (int i = 0; i < MAX_PLAYERS; i++) {
            if (local_to_netplay[i] >= 0) {
                count++;
            }
        }
        return count;
    }

    std::array<int8_t, MAX_PLAYERS> local_to_netplay;
};
