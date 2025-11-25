#include <stdio.h>
#include <dirent.h>
#include <unistd.h>

int main(int argc, char **argv)
{
    const char *path = ".";
    if (argc > 1)
    {
        path = argv[1];
    }

    DIR *dir = opendir(path);
    if (!dir)
    {
        printf("Failed to open directory: %s\n", path);
        return 1;
    }

    struct dirent *entry;
    while ((entry = readdir(dir)) != nullptr)
    {
        printf("%s\n", entry->d_name);
    }

    closedir(dir);
    return 0;
}
