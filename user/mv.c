#include <stdio.h>

int main(const int argc, char *argv[])
{
    if (argc != 3) {
        printf("Usage: mv source dest\n");
        exit();
    }

    if (rename(argv[1], argv[2]) < 0) {
        printf("mv: %s -> %s failed\n", argv[1], argv[2]);
        return 1;
    }

    return 0;
}