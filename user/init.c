#include <stdio.h>
#include <unistd.h>

int main(void)
{
    printf("Init process started (PID %d)\n", getpid());

    while (1)
    {
        printf("Starting shell...\n");
        int pid = spawn("/shell");
        if (pid > 0)
        {
            int status;
            wait(&status);
            printf("Shell exited with status %d\n", status);
        }
        else
        {
            printf("Failed to start shell\n");
            // Avoid busy loop if spawn fails repeatedly
            for (volatile int i = 0; i < 10000000; i++)
                ;
        }
    }
    return 0;
}
