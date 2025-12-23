#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>

[[noreturn]] int main(void)
{
    printf("Init process started (PID %d)\n", getpid());

    while (1)
    {
        printf("Starting shell...\n");
        const int pid = fork();
        if (pid == 0)
        {
            exec("/bin/sh");
            printf("Failed to exec shell\n");
            exit(1);
        }
        else if (pid > 0)
        {
            int status;
            wait(&status);
            printf("Shell exited with status %d\n", status);
        }
        else
        {
            printf("Failed to fork\n");
        }
    }
}
