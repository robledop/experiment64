#include "fcntl.h"
#include "unistd.h"
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <e64.h>

int main(const int argc, char** argv)
{
    char current_directory[MAX_FILE_PATH];
    getcwd(current_directory, MAX_FILE_PATH);

    if (argc != 2)
    {
        printf("\nUsage: touch <path>");
        return -EINVARG;
    }

    char full_path[MAX_FILE_PATH];
    char dir[MAX_FILE_PATH];
    strncpy(dir, argv[1], MAX_FILE_PATH);

    int res = 0;

    if (starts_with("/", dir))
    {
        res = open(dir, O_CREATE);
    }
    else
    {
        strncpy(full_path, current_directory, MAX_FILE_PATH);
        strcat(full_path, "/");
        strcat(full_path, dir);
        res = open(full_path, O_CREATE);
    }

    if (res < 0)
    {
        printf("\nFailed to create folder: %s", full_path);
        errno = res;
        perror("\nError ");

        return res;
    }

    return 0;
}
