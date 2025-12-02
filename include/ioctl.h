#pragma once

#include <stdint.h>

// Terminal window size
#define TIOCGWINSZ 0x5413

struct winsize
{
    uint16_t ws_row;    // rows, in characters
    uint16_t ws_col;    // columns, in characters
    uint16_t ws_xpixel; // width, in pixels
    uint16_t ws_ypixel; // height, in pixels
};

// Framebuffer queries
#define FB_IOCTL_GET_WIDTH 0x4600
#define FB_IOCTL_GET_HEIGHT 0x4601
#define FB_IOCTL_GET_FBADDR 0x4602
#define FB_IOCTL_GET_PITCH 0x4603

// Keyboard ioctls
#define KDFLUSH 0x4B00  // Flush keyboard input buffers
