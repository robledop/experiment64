#include <stdio.h>

int main(int argc, char** argv)
{
    if (argc < 2)
    {
        printf( "usage: kill pid...\n");
        exit();
    }
    for (int i = 1; i < argc; i++) {
        kill(atoi(argv[i]), 0);
    }
}