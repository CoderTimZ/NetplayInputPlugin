#pragma once

#include "stdafx.h"

#include "common.h"

struct controller_map {
    uint16_t map = 0x0000;

    void clear() {
        map = 0x0000;
    }

    int count() const {
        int result = 0;
        auto m = map;
        while (m) {
            result += (m & 1);
            m >>= 1;
        }
        return result;
    }

    bool is_empty() const {
        return map == 0;
    }

    bool get(int src, int dst) const {
        if (src < 0 || src >= MAX_PLAYERS) return false;
        if (dst < 0 || dst >= MAX_PLAYERS) return false;
        return (map & (1 << (src * MAX_PLAYERS + dst))) != 0;
    }

    void set(int src, int dst, bool value = true) {
        if (src < 0 || src >= MAX_PLAYERS) return;
        if (dst < 0 || dst >= MAX_PLAYERS) return;
        if (value) {
            map |= (1u << (src * MAX_PLAYERS + dst));
        } else {
            map &= ~(1u << (src * MAX_PLAYERS + dst));
        }
    }

    int to_src(int dst) const {
        if (dst < 0 || dst >= MAX_PLAYERS) return -1;
        for (int src = 0; src < MAX_PLAYERS; src++) {
            if (get(src, dst)) {
                return src;
            }
        }
        return -1;
    }
};