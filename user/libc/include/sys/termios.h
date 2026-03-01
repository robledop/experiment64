#pragma once

#include <stdint.h>

typedef uint32_t tcflag_t;
typedef unsigned char cc_t;
typedef uint32_t speed_t;

#define NCCS 16

#define IGNBRK 0x00000800
#define ECHO 0x0001
#define ICANON 0x0002
#define IXON 0x0004
#define ICRNL 0x0008
#define OPOST 0x0010
#define CSIZE 0x0030
#define CS5 0x0000
#define CS6 0x0010
#define CS7 0x0020
#define CS8 0x0030
#define ISIG 0x0040
#define ISTRIP 0x0080
#define INPCK 0x0100
#define IEXTEN 0x0200
#define BRKINT 0x0400
#define IGNPAR 0x00001000
#define PARMRK 0x00002000
#define INLCR 0x00004000
#define IGNCR 0x00008000
#define IXOFF 0x00010000
#define IXANY 0x00020000

#define ONLCR 0x00040000
#define OCRNL 0x00080000
#define ONOCR 0x00100000
#define ONLRET 0x00200000

#define CSTOPB 0x00400000
#define CREAD 0x00800000
#define PARENB 0x01000000
#define PARODD 0x02000000
#define HUPCL 0x04000000
#define CLOCAL 0x08000000

#define ECHOE 0x10000000
#define ECHOK 0x20000000
#define ECHONL 0x40000000
#define NOFLSH 0x80000000u

#define VMIN 0
#define VTIME 1
#define VINTR 2
#define VQUIT 3
#define VERASE 4
#define VKILL 5
#define VEOF 6
#define VEOL 7
#define VSTART 8
#define VSTOP 9
#define VSUSP 10
#define VREPRINT 11
#define VDISCARD 12
#define VWERASE 13
#define VLNEXT 14
#define VEOL2 15

#define TCSANOW 0
#define TCSADRAIN 1
#define TCSAFLUSH 2

struct termios
{
    tcflag_t c_iflag;
    tcflag_t c_oflag;
    tcflag_t c_cflag;
    tcflag_t c_lflag;
    cc_t c_cc[NCCS];
};
