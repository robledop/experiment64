#pragma once

#include <sys/poll.h>

int poll(struct pollfd *fds, nfds_t nfds, int timeout);
