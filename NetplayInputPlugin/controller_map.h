#pragma once

#include "stdafx.h"

#include "common.h"

struct controller_map {
    uint16_t bits = 0;

    constexpr controller_map() : controller_map(0) { }

    constexpr controller_map(uint16_t bits) : bits(bits) { }

    bool empty() const {
        return bits == 0;
    }

    bool get(uint8_t src, uint8_t dst) const {
        if ((src | dst) >= 4) return false;
        return bits & (1 << (src * 4 + dst));
    }

    void set(uint8_t src, uint8_t dst) {
        if ((src | dst) >= 4) return;
        bits |= (1 << (src * 4 + dst));
    }

    void clear() {
        bits = 0;
    }
};