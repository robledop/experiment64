#include <signal.h>
#include <stdio.h>
#include <unistd.h>

static volatile int got_signal = 0;
static volatile int last_signal = 0;

static void sig_handler(const int sig)
{
    last_signal = sig;
    got_signal = 1;
}

int main(void)
{
    struct sigaction sa = {nullptr};
    sa.sa_handler = sig_handler;

    if (sigaction(SIGTERM, &sa, nullptr) < 0)
    {
        printf("sigaction(SIGTERM) failed\n");
        return 1;
    }
    if (sigaction(SIGUSR1, &sa, nullptr) < 0)
    {
        printf("sigaction(SIGUSR1) failed\n");
        return 1;
    }

    printf("sigaction_demo: pid=%d\n", getpid());
    printf("Send SIGTERM (kill <pid>) or SIGUSR1 (kill -10 <pid>)\n");

    while (!got_signal)
    {
        usleep(200000);
    }

    printf("sigaction_demo: received signal %d, exiting.\n", last_signal);
    return 0;
}
