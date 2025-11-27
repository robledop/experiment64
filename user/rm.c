#include <stdio.h>
int main(int argc, char *argv[])
{
    if (argc < 2) {
        printf("Usage: rm files...\n");
        exit();
    }

    for (int i = 1; i < argc; i++) {
        if (unlink(argv[i]) < 0) {
            printf("rm: %s failed to delete\n", argv[i]);
            break;
        }
    }

    return 0;
}