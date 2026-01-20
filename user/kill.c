#include <signal.h>
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char** argv)
{
    if (argc < 2)
    {
        printf("usage: kill [-SIGNAL] pid...\n");
        exit();
    }

    int sig = SIGTERM;
    int first_pid = 1;
    if (argv[1][0] == '-')
    {
        sig = (int)strtol(argv[1] + 1, nullptr, 10);
        if (sig <= 0 || argc < 3)
        {
            printf("usage: kill [-SIGNAL] pid...\n");
            exit();
        }
        first_pid = 2;
    }

    for (int i = first_pid; i < argc; i++)
    {
        kill((int)strtol(argv[i], nullptr, 10), sig);
    }

    return 0;
}
