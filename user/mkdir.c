#include <stdio.h>
#include <fcntl.h>

int main(int argc, char *argv[])
{
    if (argc < 2) {
        printf("Usage: mkdir files...\n");
        exit();
    }

    for (int i = 1; i < argc; i++) {
        if (mkdir(argv[i], O_CREATE) < 0) {
            printf("mkdir: %s failed to create\n", argv[i]);
            break;
        }
    }

    return 0;
}
