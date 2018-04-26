#pragma once

#include <cstdint>
#include <array>

class controller_map {
public:
    controller_map() {
        local_to_netplay.fill(-1);
    }

    int to_netplay(int local_controller) const {
        if (0 <= local_controller && local_controller < 4) {
            return local_to_netplay[local_controller];
        } else {
            return -1;
        }
    }

    int to_local(int netplay_controller) const {
        for (int i = 0; i < 4; i++) {
            if (local_to_netplay[i] == netplay_controller) {
                return i;
            }
        }
        return -1;
    }

    void map(int local_controller, int netplay_controller) {
        if (0 <= local_controller && local_controller < 4) {
            local_to_netplay[local_controller] = netplay_controller;
        }
    }

    int local_count() const {
        int count = 0;
        for (int i = 0; i < 4; i++) {
            if (local_to_netplay[i] >= 0) {
                count++;
            }
        }
        return count;
    }

    std::array<int8_t, 4> local_to_netplay;
};
