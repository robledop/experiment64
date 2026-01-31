#include <signal.h>
#include <stdio.h>
#include <unistd.h>

static volatile int got_sigchld = 0;

static void sigchld_handler([[maybe_unused]] const int sig)
{
    got_sigchld = 1;
}

int main(void)
{
    struct sigaction sa = {};
    sa.sa_handler       = sigchld_handler;
    sa.sa_flags         = SA_NOCLDWAIT;
    if (sigaction(SIGCHLD, &sa, nullptr) < 0) {
        printf("sigtest_nocldwait: sigaction failed\n");
        return 1;
    }

    const int pid = fork();
    if (pid < 0) {
        printf("sigtest_nocldwait: fork failed\n");
        return 2;
    }
    if (pid == 0) {
        return 0;
    }

    for (int i = 0; i < 100000; i++) {
        if (got_sigchld)
            break;
        yield();
    }

    if (!got_sigchld) {
        printf("sigtest_nocldwait: handler not invoked\n");
        return 3;
    }

    // With SA_NOCLDWAIT, wait should return -1
    int status       = 0;
    const int waited = wait(&status);
    if (waited >= 0) {
        printf("sigtest_nocldwait: wait returned %d (expected -1)\n", waited);
        return 4;
    }

    return 12;
}