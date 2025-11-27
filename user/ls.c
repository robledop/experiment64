#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void print_entry(const char *name, const char *full_path)
{
    // Treat an entry as a directory if we can open it with opendir.
    DIR *maybe_dir = opendir(full_path);
    const int is_dir = (maybe_dir != nullptr);
    if (maybe_dir)
        closedir(maybe_dir);

    if (is_dir)
        printf("%s/\n", name);
    else
        printf("%s\n", name);
}

static int list_dir(const char *path)
{
    DIR *dir = opendir(path);
    if (!dir)
    {
        printf("ls: cannot open %s\n", path);
        return 1;
    }

    struct dirent *entry;
    char full_path[512];
    const size_t base_len = strlen(path);
    while ((entry = readdir(dir)) != nullptr)
    {
        // Build "path/name" for directory detection.
        size_t name_len = strlen(entry->d_name);
        if (base_len + 1 + name_len + 1 >= sizeof(full_path))
        {
            printf("ls: path too long: %s/%s\n", path, entry->d_name);
            continue;
        }
        memcpy(full_path, path, base_len);
        size_t idx = base_len;
        if (idx == 0 || path[idx - 1] != '/')
            full_path[idx++] = '/';
        memcpy(full_path + idx, entry->d_name, name_len + 1);

        print_entry(entry->d_name, full_path);
    }

    closedir(dir);
    return 0;
}

int main(int argc, char **argv)
{
    if (argc < 2)
        return list_dir(".");

    int ret = 0;
    for (int i = 1; i < argc; i++)
        ret |= list_dir(argv[i]);
    return ret;
}
