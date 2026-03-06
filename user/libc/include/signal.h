#pragma once

#include <sys/signal.h>

int sigaction(int signum, const struct sigaction* act, struct sigaction* oldact);
sighandler_t signal(int signum, sighandler_t handler);
int sigprocmask(int how, const sigset_t* set, sigset_t* oldset);
int sigemptyset(sigset_t *set);
int sigfillset(sigset_t *set);
int sigaddset(sigset_t *set, int signum);
int sigdelset(sigset_t *set, int signum);
int sigismember(const sigset_t *set, int signum);
int sigsuspend(const sigset_t *sigmask);
int raise(int sig);
char *strsignal(int sig);
