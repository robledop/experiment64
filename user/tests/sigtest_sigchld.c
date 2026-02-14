#include <signal.h>
#include <stdio.h>
#include <unistd.h>

static volatile int got_sigchld = 0;

static void sigchld_handler([[maybe_unused]] const int sig)
{
    got_sigchld++;
}

int main(void)
{
    struct sigaction sa = {};
    sa.sa_handler       = sigchld_handler;
    if (sigaction(SIGCHLD, &sa, nullptr) < 0) {
        printf("sigtest_sigchld: sigaction failed\n");
        return 1;
    }

    const int pid = fork();
    if (pid < 0) {
        printf("sigtest_sigchld: fork failed\n");
        return 2;
    }
    if (pid == 0) {
        return 0;
    }

    for (int i = 0; i < 100000; i++) {
        if (got_sigchld > 0)
            break;
        yield(); // To give an opportunity for the child process to exit
    }

    if (got_sigchld == 0) {
        printf("sigtest_sigchld: handler not invoked\n");
        return 3;
    }

    int status       = 0;
    const int waited = wait(&status);
    if (waited != pid) {
        printf("sigtest_sigchld: wait returned %d (expected %d)\n", waited, pid);
        return 4;
    }
    if (status != 0) {
        printf("sigtest_sigchld: unexpected child status %d\n", status);
        return 5;
    }

    // We only reach this point if the child process exited successfully and
    // the parent process received the SIGCHLD signal.
    return 10;
}