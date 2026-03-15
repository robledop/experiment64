#include <fs/vfs.h>
#include <mem/heap.h>

vfs_inode_t *vfs_root = nullptr;

void vfs_init()
{
    vfs_root = nullptr;
}

static void vfs_put_inode(vfs_inode_t *node)
{
    if (!node || node == vfs_root)
        return;
    vfs_close(node);
    kfree(node);
}

void vfs_release(vfs_inode_t *node)
{
    if (!node || node == vfs_root)
        return;

    if (node->ref > 1) {
        node->ref--;
        return;
    }

    vfs_put_inode(node);
}

uint64_t vfs_read(const vfs_inode_t *node, uint64_t offset, uint64_t size, uint8_t *buffer)
{
    if (node->iops && node->iops->read)
        return node->iops->read(node, offset, size, buffer);
    return 0;
}

uint64_t vfs_write(vfs_inode_t *node, uint64_t offset, uint64_t size, uint8_t *buffer)
{
    if (node->iops && node->iops->write)
        return node->iops->write(node, offset, size, buffer);
    return 0;
}

int vfs_truncate(vfs_inode_t *node)
{
    if (node->iops && node->iops->truncate)
        return node->iops->truncate(node);
    return -1;
}

int vfs_ioctl(vfs_inode_t *node, int request, void *arg)
{
    if (node->iops && node->iops->ioctl)
        return node->iops->ioctl(node, request, arg);
    return -1;
}

void vfs_open(const vfs_inode_t *node)
{
    if (node->iops && node->iops->open)
        node->iops->open(node);
}

void vfs_close(vfs_inode_t *node)
{
    if (node->iops && node->iops->close)
        node->iops->close(node);
}

int vfs_poll(const vfs_inode_t *node, short events, short *revents)
{
    if (!node || !revents)
        return -1;
    if (node->iops && node->iops->poll)
        return node->iops->poll(node, events, revents);
    return -1;
}
