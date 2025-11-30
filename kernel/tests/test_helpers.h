#pragma once

#include "vfs.h"
#include "heap.h"
#include "string.h"
#include "terminal.h"

// Ensure a regular file exists at path; creates it if missing.
static inline vfs_inode_t* test_vfs_ensure_file(const char* path)
{
    vfs_inode_t* node = vfs_resolve_path(path);
    if (!node)
    {
        vfs_mknod((char*)path, VFS_FILE, 0);
        node = vfs_resolve_path(path);
    }
    return node;
}

// Write full buffer to path at offset, ensuring the file exists. Frees inode.
static inline bool test_vfs_write_file(const char* path, uint64_t offset, const void* data, size_t len)
{
    vfs_inode_t* node = test_vfs_ensure_file(path);
    if (!node)
    {
        printk("test_vfs_write_file: failed to open %s\n", path);
        return false;
    }
    bool ok = vfs_write(node, offset, len, (uint8_t*)data) == len;
    vfs_close(node);
    kfree(node);
    return ok;
}
