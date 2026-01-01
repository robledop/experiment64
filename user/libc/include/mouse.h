#pragma once

#include <stdint.h>

#define MOUSE_LEFT (1 << 0)
#define MOUSE_RIGHT (1 << 1)
#define MOUSE_MIDDLE (1 << 2)

struct ps2_mouse_packet
{
    uint8_t flags;
    int16_t x;
    int16_t y;
};

