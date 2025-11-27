#include "devfs.h"
#include "heap.h"
#include "string.h"

#define MAX_DEVICES 32

struct device_entry
{
    char name[64];
    vfs_inode_t *inode;
};

static struct device_entry device_registry[MAX_DEVICES];
static int device_count = 0;

vfs_inode_t *devfs_finddir([[maybe_unused]] const vfs_inode_t *node, const char *name)
{
    for (int i = 0; i < device_count; i++)
    {
        if (strcmp(name, device_registry[i].name) == 0)
        {
            vfs_inode_t *copy = kmalloc(sizeof(vfs_inode_t));
            if (copy)
            {
                memcpy(copy, device_registry[i].inode, sizeof(vfs_inode_t));
            }
            return copy;
        }
    }
    return nullptr;
}

vfs_dirent_t *devfs_readdir([[maybe_unused]] const vfs_inode_t *node, uint32_t index)
{
    if (index < (uint32_t)device_count)
    {
        vfs_dirent_t *dirent = kmalloc(sizeof(vfs_dirent_t));
        strcpy(dirent->name, device_registry[index].name);
        dirent->inode = 0; // Dummy
        return dirent;
    }
    return nullptr;
}

struct inode_operations devfs_ops = {
    .finddir = devfs_finddir,
    .readdir = devfs_readdir,
};

void devfs_register_device(const char *name, vfs_inode_t *device_node)
{
    if (device_count >= MAX_DEVICES || !name || !device_node)
        return;

    // Insert in lexicographic order to keep stable directory listing.
    int idx = 0;
    for (; idx < device_count; idx++)
    {
        if (strcmp(name, device_registry[idx].name) < 0)
            break;
    }

    // Shift to make room.
    for (int j = device_count; j > idx; j--)
    {
        device_registry[j] = device_registry[j - 1];
    }

    strncpy(device_registry[idx].name, name, 63);
    device_registry[idx].name[63] = '\0';
    device_registry[idx].inode = device_node;
    device_count++;
}

void devfs_init(void)
{
    // Create /dev directory node (the mount root)
    vfs_inode_t *dev_root = kmalloc(sizeof(vfs_inode_t));
    memset(dev_root, 0, sizeof(vfs_inode_t));
    dev_root->flags = VFS_DIRECTORY;
    dev_root->iops = &devfs_ops;

    vfs_register_mount("dev", dev_root);
}
