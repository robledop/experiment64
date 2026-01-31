#include <stdio.h>
#include <unistd.h>

int main(void)
{
    const int pid = fork();
    if (pid < 0) {
        printf("waitpid_test: fork failed\n");
        return 1;
    }
    if (pid == 0) {
        return 42;
    }

    int status = 0;
    int rc = waitpid(pid, &status, 1);
    if (rc != -1) {
        printf("waitpid_test: invalid options rc=%d\n", rc);
        return 2;
    }

    const int waited = waitpid(pid, &status, 0);
    if (waited != pid) {
        printf("waitpid_test: waitpid returned %d (expected %d)\n", waited, pid);
        return 3;
    }
    if (status != 42) {
        printf("waitpid_test: unexpected status %d\n", status);
        return 4;
    }

    rc = waitpid(pid, &status, 0);
    if (rc >= 0) {
        printf("waitpid_test: second waitpid returned %d\n", rc);
        return 5;
    }

    return 14;
}
