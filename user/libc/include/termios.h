#pragma once

#include <sys/termios.h>

#define TCSAFLUSH 2

// Internal helper used by libc to inspect output flags per FD.
tcflag_t __termios_get_oflag(int fd);

int tcgetattr(int fd, struct termios *termios_p);
int tcsetattr(int fd, int optional_actions, const struct termios *termios_p);
