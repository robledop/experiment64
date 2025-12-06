#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv)
{
    if (argc < 2)
    {
        printf("usage: kill pid...\n");
        exit();
    }
    for (int i = 1; i < argc; i++)
    {
        kill((int)strtol(argv[i], nullptr, 10), 0);
    }
}