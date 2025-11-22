#include "vfs.h"
#include "string.h"
#include "terminal.h"

vfs_inode_t *vfs_root = 0;

void vfs_init()
{
    // Initialize root as null or a temporary placeholder if needed
    vfs_root = 0;
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
