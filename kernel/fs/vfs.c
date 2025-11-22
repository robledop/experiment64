#include "vfs.h"
#include "string.h"
#include "terminal.h"
#include "fat32.h"

vfs_inode_t *vfs_root = 0;

void vfs_init()
{
    // Initialize root as null or a temporary placeholder if needed
    vfs_root = 0;
}

void vfs_mount_root(void)
{
    // Mount FAT32 partition (Drive 0, Partition 1 - LBA 2048)
    // TODO: Use GPT to find this dynamically
    vfs_root = fat32_mount(0, 2048);
    if (vfs_root)
    {
        printf("VFS: Mounted FAT32 on /\n");
    }
    else
    {
        printf("VFS: Failed to mount FAT32\n");
    }
}

uint64_t vfs_read(vfs_inode_t *node, uint64_t offset, uint64_t size, uint8_t *buffer)
{
    if (node->read)
        return node->read(node, offset, size, buffer);
    return 0;
}

uint64_t vfs_write(vfs_inode_t *node, uint64_t offset, uint64_t size, uint8_t *buffer)
{
    if (node->write)
        return node->write(node, offset, size, buffer);
    return 0;
}

void vfs_open(vfs_inode_t *node)
{
    if (node->open)
        node->open(node);
}

void vfs_close(vfs_inode_t *node)
{
    if (node->close)
        node->close(node);
}

vfs_dirent_t *vfs_readdir(vfs_inode_t *node, uint32_t index)
{
    if ((node->flags & 0x07) == VFS_DIRECTORY && node->readdir)
        return node->readdir(node, index);
    return 0;
}

vfs_inode_t *vfs_finddir(vfs_inode_t *node, char *name)
{
    if ((node->flags & 0x07) == VFS_DIRECTORY && node->finddir)
        return node->finddir(node, name);
    return 0;
}

vfs_inode_t *vfs_resolve_path(const char *path)
{
    if (!path || !vfs_root)
        return 0;

    vfs_inode_t *current = vfs_root;

    // Handle absolute paths (treat as relative to root for now)
    while (*path == '/')
        path++;

    if (*path == 0)
        return current; // Root

    char name[128]; // Max filename length
    int name_idx = 0;

    while (*path)
    {
        if (*path == '/')
        {
            name[name_idx] = 0;
            if (name_idx > 0)
            {
                vfs_inode_t *next = vfs_finddir(current, name);
                if (!next)
                    return 0;
                current = next;
            }
            name_idx = 0;
        }
        else
        {
            if (name_idx < 127)
                name[name_idx++] = *path;
        }
        path++;
    }

    // Last component
    if (name_idx > 0)
    {
        name[name_idx] = 0;
        vfs_inode_t *next = vfs_finddir(current, name);
        if (!next)
            return 0;
        current = next;
    }

    return current;
}
