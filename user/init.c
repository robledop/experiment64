#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>

int main(void)
{
    printf("Init process started (PID %d)\n", getpid());

    while (1)
    {
        printf("Starting shell...\n");
        int pid = fork();
        if (pid == 0)
        {
            exec("/bin/shell");
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
            // Avoid busy loop if fork fails repeatedly
            for (volatile int i = 0; i < 10000000; i++)
                ;
        }
    }
    return 0;
}
