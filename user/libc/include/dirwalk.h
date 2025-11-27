#pragma once

#include <stddef.h>
#include <stdint.h>

#define EXT2_DIRENT_NAME_MAX 255

struct dirent_view
{
    const char *name;
    size_t name_len;
    uint32_t inode;
};

// Walk directory entries on an open directory file descriptor.
// The callback receives a lightweight view of each entry.
// Return 0 on success, -1 on callback error.
int dirwalk(int fd, int (*fn)(const struct dirent_view *entry, void *arg), void *arg);
