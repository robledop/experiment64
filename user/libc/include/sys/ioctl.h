#ifndef _SYS_IOCTL_H
#define _SYS_IOCTL_H

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

int ioctl(int fd, unsigned long request, void *arg);

#endif
