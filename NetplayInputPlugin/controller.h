#pragma once

#include <cstdint>

namespace controller {
    typedef struct {
        uint32_t Present;
        uint32_t RawData;
        uint32_t Plugin;
    } CONTROL;

    typedef union {
        uint32_t Value;
        struct {
            unsigned R_DPAD : 1;
            unsigned L_DPAD : 1;
            unsigned D_DPAD : 1;
            unsigned U_DPAD : 1;
            unsigned START_BUTTON : 1;
            unsigned Z_TRIG : 1;
            unsigned B_BUTTON : 1;
            unsigned A_BUTTON : 1;

            unsigned R_CBUTTON : 1;
            unsigned L_CBUTTON : 1;
            unsigned D_CBUTTON : 1;
            unsigned U_CBUTTON : 1;
            unsigned R_TRIG : 1;
            unsigned L_TRIG : 1;
            unsigned Reserved1 : 1;
            unsigned Reserved2 : 1;

            signed   Y_AXIS : 8;

            signed   X_AXIS : 8;
        };
    } BUTTONS;
}
