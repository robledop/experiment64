#pragma once

#include <stdint.h>

#define TIOCGWINSZ 0x5413

struct winsize
{
    uint16_t ws_row;
    uint16_t ws_col;
    uint16_t ws_xpixel;
    uint16_t ws_ypixel;
};

#define FB_IOCTL_GET_WIDTH 0x4600
#define FB_IOCTL_GET_HEIGHT 0x4601
#define FB_IOCTL_GET_FBADDR 0x4602
#define FB_IOCTL_GET_PITCH 0x4603

// Keyboard ioctl
#define KDFLUSH 0x4B00 // Flush keyboard input buffers


struct netinfo
{
    uint8_t mac[6];
    uint32_t ip;
    uint32_t subnet_mask;
    uint32_t default_gateway;
    uint32_t dns_server;
};

// Network ioctl
#define GETNETINFO 0x4090
