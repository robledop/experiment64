#pragma once

#include <stdint.h>

typedef uint32_t tcflag_t;
typedef unsigned char cc_t;

#define NCCS 8

#define ECHO 0x0001
#define ICANON 0x0002
#define IXON 0x0004
#define ICRNL 0x0008
#define OPOST 0x0010

#define VMIN 0
#define VTIME 1

#define TCSAFLUSH 2

struct termios
{
    tcflag_t c_iflag;
    tcflag_t c_oflag;
    tcflag_t c_cflag;
    tcflag_t c_lflag;
    cc_t c_cc[NCCS];
};

int tcgetattr(int fd, struct termios *termios_p);
int tcsetattr(int fd, int optional_actions, const struct termios *termios_p);
