#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>
#include <sys/stat.h>

// Simple color helpers (ANSI); safe to adjust or drop if needed.
#define COLOR_RESET "\033[0m"
#define COLOR_DIR   "\033[34m"
#define COLOR_DEV   "\033[33m"
#define COLOR_FILE  "\033[37m"

static void print_u64(uint64_t n)
{
    char buf[32];
    int i = 0;
    if (n == 0)
    {
        putchar('0');
        return;
    }
    while (n > 0 && i < (int)sizeof(buf))
    {
        buf[i++] = (char)('0' + (n % 10));
        n /= 10;
    }
    while (i-- > 0)
        putchar(buf[i]);
}

static void print_spaces(int count)
{
    while (count-- > 0)
        putchar(' ');
}

static int u64_to_str(uint64_t n, char *buf, size_t cap)
{
    if (cap == 0)
        return 0;
    if (n == 0)
    {
        if (cap > 1)
            buf[0] = '0', buf[1] = 0;
        else
            buf[0] = 0;
        return 1;
    }
    size_t i = 0;
    while (n > 0 && i + 1 < cap)
    {
        buf[i++] = (char)('0' + (n % 10));
        n /= 10;
    }
    if (i >= cap)
        i = cap - 1;
    buf[i] = 0;
    // reverse
    for (size_t l = 0, r = i - 1; l < r; l++, r--)
    {
        char tmp = buf[l];
        buf[l] = buf[r];
        buf[r] = tmp;
    }
    return (int)i;
}

static void format_human_size(uint64_t bytes, char *out, size_t out_sz)
{
    const char *units[] = {"B", "KB", "MB", "GB", "TB"};
    size_t unit_index = 0;
    uint64_t scale = 1;
    uint64_t whole = bytes;
    while (whole >= 1024 && unit_index + 1 < (sizeof units / sizeof units[0]))
    {
        scale *= 1024;
        unit_index++;
        whole = bytes / scale;
    }

    uint64_t rem = bytes - (whole * scale);
    uint64_t frac = (unit_index > 0) ? (rem * 100) / scale : 0;

    char *p = out;
    size_t remaining = out_sz;

    // whole
    int len = u64_to_str(whole, p, remaining);
    p += len;
    remaining = (remaining > (size_t)len) ? (remaining - len) : 0;

    if (remaining > 0 && frac > 0)
    {
        *p++ = '.';
        remaining--;
        if (remaining > 0)
        {
            if (frac < 10)
            {
                *p++ = '0';
                remaining--;
            }
            len = u64_to_str(frac, p, remaining);
            p += len;
            remaining = (remaining > (size_t)len) ? (remaining - len) : 0;
        }
    }

    if (remaining > 1)
    {
        *p++ = ' ';
        remaining--;
    }

    const char *unit = units[unit_index];
    while (remaining > 1 && *unit)
    {
        *p++ = *unit++;
        remaining--;
    }
    *p = 0;
}

static void print_padded_str(const char *s, int width)
{
    int len = (int)strlen(s);
    int pad = width - len;
    if (pad < 0)
        pad = 0;
    print_spaces(pad);
    while (*s)
        putchar(*s++);
}

static void print_padded_u64(uint64_t val, int width)
{
    char buf[32];
    u64_to_str(val, buf, sizeof buf);
    print_padded_str(buf, width);
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
    char size_buf[32];
    format_human_size(st->size, size_buf, sizeof size_buf);

    printf("%s ", type_char);
    print_padded_u64((uint64_t)st->ino, 6);
    putchar(' ');
    print_padded_str(size_buf, 9);
    putchar(' ');
    print_padded_u64(st->i_mtime, 10);
    printf(" %s%s%s\n", color, name, COLOR_RESET);
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
