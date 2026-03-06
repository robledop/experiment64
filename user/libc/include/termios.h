#pragma once

#include <sys/termios.h>

// Internal helper used by libc to inspect output flags per FD.
tcflag_t __termios_get_oflag(int fd);
void cfmakeraw(struct termios *termios_p);

int tcgetattr(int fd, struct termios *termios_p);
int tcsetattr(int fd, int optional_actions, const struct termios *termios_p);
int tcflush(int fd, int queue_selector);
