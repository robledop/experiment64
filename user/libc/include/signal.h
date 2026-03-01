#pragma once

#include <sys/signal.h>

int sigaction(int signum, const struct sigaction* act, struct sigaction* oldact);
sighandler_t signal(int signum, sighandler_t handler);
int sigprocmask(int how, const sigset_t* set, sigset_t* oldset);
