#include <signal.h>
#include <stdio.h>
#include <unistd.h>

static volatile int got_signal = 0;

static void sigtest_handler(const int sig)
{
    got_signal = sig;
}

int main(void)
{
    struct sigaction sa = {nullptr};
    sa.sa_handler = sigtest_handler;
    if (sigaction(SIGUSR1, &sa, nullptr) < 0)
    {
        printf("sigtest_handler: sigaction failed\n");
        return 1;
    }

    const int pid = getpid();
    if (kill(pid, SIGUSR1) < 0)
    {
        printf("sigtest_handler: kill failed\n");
        return 2;
    }

    if (got_signal != SIGUSR1)
    {
        printf("sigtest_handler: handler not invoked\n");
        return 3;
    }

    return 0;
}
