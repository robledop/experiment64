#include <signal.h>
#include <errno.h>
#include <unistd.h>

static volatile sig_atomic_t got_winch = 0;

static void sigwinch_handler(const int sig)
{
    got_winch = sig;
}

int main(void)
{
    sigset_t set = 0;

    if (sigemptyset(&set) != 0)
        return 1;
    if (sigaddset(&set, SIGWINCH) != 0)
        return 2;
    if (sigismember(&set, SIGWINCH) != 1)
        return 3;
    if (sigdelset(&set, SIGWINCH) != 0)
        return 4;
    if (sigismember(&set, SIGWINCH) != 0)
        return 5;
    if (sigfillset(&set) != 0)
        return 6;
    if (sigismember(&set, SIGWINCH) != 1)
        return 7;

    errno = 0;
    if (sigaddset(&set, 0) != -1 || errno != EINVAL)
        return 8;

    errno = 0;
    if (sigismember(nullptr, SIGWINCH) != -1 || errno != EINVAL)
        return 9;

    struct sigaction sa = {};
    sa.sa_handler       = sigwinch_handler;
    if (sigemptyset(&sa.sa_mask) != 0)
        return 10;
    if (sigaction(SIGWINCH, &sa, nullptr) < 0)
        return 11;
    if (kill(getpid(), SIGWINCH) < 0)
        return 12;
    if (got_winch != SIGWINCH)
        return 13;

    return 0;
}
