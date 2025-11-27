#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

// Simple color helpers (ANSI); safe to adjust or drop if needed.
#define COLOR_RESET "\033[0m"
#define COLOR_DIR   "\033[34m"
#define COLOR_DEV   "\033[33m"
#define COLOR_FILE  "\033[37m"

static void print_human_size(uint64_t bytes)
{
    const char *units[] = {"B", "KB", "MB", "GB", "TB"};
    size_t unit_index = 0;
    double size = (double)bytes;

    while (size >= 1024.0 && unit_index + 1 < (sizeof units / sizeof units[0]))
    {
        size /= 1024.0;
        unit_index++;
    }

    // Our libc is minimal; format directly via printf.
    printf("%.2f %s", size, units[unit_index]);
}

static void print_entry_detailed(const char *name, const char *full_path, const struct stat *st)
{
    const char *type_char = "-";
    const char *color = COLOR_FILE;
    switch (st->type)
    {
    case T_DIR:
        type_char = "d";
        color = COLOR_DIR;
        break;
    case T_DEV:
        type_char = "c";
        color = COLOR_DEV;
        break;
    default:
        type_char = "-";
        color = COLOR_FILE;
        break;
    }

    // We don't yet have full time formatting; show raw mtime for now.
    printf("%s %5d ", type_char, st->ino);
    print_human_size(st->size);
    printf(" %10u %s%s%s\n",
           st->i_mtime,
           color,
           name,
           COLOR_RESET);
    (void)full_path;
}

static void print_entry(const char *name, const char *full_path)
{
    struct stat st;
    if (stat(full_path, &st) == 0)
    {
        print_entry_detailed(name, full_path, &st);
        return;
    }

    // Fallback if stat fails.
    DIR *maybe_dir = opendir(full_path);
    const int is_dir = (maybe_dir != nullptr);
    if (maybe_dir)
        closedir(maybe_dir);
    printf("%s%s%s%s\n", is_dir ? COLOR_DIR : COLOR_FILE, name, is_dir ? "/" : "", COLOR_RESET);
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
