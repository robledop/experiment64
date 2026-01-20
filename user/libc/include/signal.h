#pragma once

#include <sys/signal.h>

int sigaction(int signum, const struct sigaction* act, struct sigaction* oldact);
sighandler_t signal(int signum, sighandler_t handler);
