#pragma once

#include <stddef.h>
#include <stdint.h>

#define EXT2_DIRENT_NAME_MAX 255

struct dirent
{
    char d_name[128];
    uint32_t d_ino;
};

typedef struct
{
    int fd;
    struct dirent cur_entry; // Buffer for readdir
} DIR;

/**
 * @brief Lightweight read-only view of a directory entry.
 *
 * Handed to the dirwalk() callback so callers see only the parts they care
 * about, without exposing the backing `struct dirent` buffer.
 */
struct dirent_view
{
    const char *name;
    size_t name_len;
    uint32_t inode;
};

DIR *opendir(const char *name);
struct dirent *readdir(DIR *dirp);
int closedir(DIR *dirp);

/**
 * @brief Walk every entry in an open directory, invoking @p fn per entry.
 *
 * @param fd Open directory file descriptor.
 * @param fn Callback receiving a `dirent_view` for each entry.
 * @param arg Opaque pointer forwarded to @p fn.
 * @return 0 on success, -1 if the callback returned negative.
 */
int dirwalk(int fd, int (*fn)(const struct dirent_view *entry, void *arg), void *arg);