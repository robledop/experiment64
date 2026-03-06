#pragma once

#include <stdint.h>

// Get terminal window size
#define TIOCGWINSZ 0x5413
// Set terminal window size
#define TIOCSWINSZ 0x5414
// Get terminal attributes
#define TIOCGETA 0x5401
// Set terminal attributes
#define TIOCSETA 0x5402
// Linux termios aliases
#define TCGETS TIOCGETA
#define TCSETS TIOCSETA
#define TCSETSW 0x5403
#define TCSETSF 0x5404
// Get foreground process
#define TIOCGPGRP 0x540F
// Set foreground process
#define TIOCSPGRP 0x5410
// Revoke (hangup) a PTY — all reads/writes return 0
#define TIOCHUP 0x5422

struct winsize
{
    uint16_t ws_row; // rows, in characters
    uint16_t ws_col; // columns, in characters
    uint16_t ws_xpixel; // width, in pixels
    uint16_t ws_ypixel; // height, in pixels
};

// Framebuffer queries
#define FB_IOCTL_GET_WIDTH 0x4600
#define FB_IOCTL_GET_HEIGHT 0x4601
#define FB_IOCTL_GET_FBADDR 0x4602
#define FB_IOCTL_GET_PITCH 0x4603

// Pipe / generic fd ioctls
#define FIONBIO 0x5421 // Set non-blocking I/O on fd

// Keyboard ioctls
#define KDFLUSH 0x4B00 // Flush keyboard input buffers

// Network ioctl
#define GETNETINFO 0x4090
