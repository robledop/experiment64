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
#define CS8 0x0030
#define ISIG 0x0040
#define ISTRIP 0x0080
#define INPCK 0x0100
#define IEXTEN 0x0200
#define BRKINT 0x0400

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

// Internal helper used by libc to inspect output flags per FD.
tcflag_t __termios_get_oflag(int fd);

int tcgetattr(int fd, struct termios *termios_p);
int tcsetattr(int fd, int optional_actions, const struct termios *termios_p);
