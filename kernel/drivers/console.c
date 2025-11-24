#include "vfs.h"
#include "keyboard.h"
#include "terminal.h"
#include "string.h"
#include "heap.h"
#include "console.h"

// Console inode operations
uint64_t console_read(vfs_inode_t *node, uint64_t offset, uint64_t size, uint8_t *buffer)
{
    (void)node;
    (void)offset;
    for (uint64_t i = 0; i < size; i++)
    {
        buffer[i] = keyboard_get_char();
    }
    return size;
}

uint64_t console_write(vfs_inode_t *node, uint64_t offset, uint64_t size, uint8_t *buffer)
{
    (void)node;
    (void)offset;
    terminal_write((char *)buffer, size);
    return size;
}

struct inode_operations console_ops = {
    .read = console_read,
    .write = console_write,
};

vfs_inode_t *console_device = NULL;

vfs_inode_t *devfs_finddir(vfs_inode_t *node, char *name)
{
    (void)node;
    if (strcmp(name, "console") == 0)
    {
        vfs_inode_t *copy = kmalloc(sizeof(vfs_inode_t));
        if (copy)
        {
            memcpy(copy, console_device, sizeof(vfs_inode_t));
        }
        return copy;
    }
    return NULL;
}

vfs_dirent_t *devfs_readdir(vfs_inode_t *node, uint32_t index)
{
    (void)node;
    if (index == 0)
    {
        vfs_dirent_t *dirent = kmalloc(sizeof(vfs_dirent_t));
        strcpy(dirent->name, "console");
        dirent->inode = 0; // Dummy
        return dirent;
    }
    return NULL;
}

struct inode_operations devfs_ops = {
    .finddir = devfs_finddir,
    .readdir = devfs_readdir,
};

void console_init()
{
    // Create console device node
    console_device = kmalloc(sizeof(vfs_inode_t));
    memset(console_device, 0, sizeof(vfs_inode_t));
    console_device->flags = VFS_CHARDEVICE;
    console_device->iops = &console_ops;

    // Create /dev directory node (the mount root)
    vfs_inode_t *dev_root = kmalloc(sizeof(vfs_inode_t));
    memset(dev_root, 0, sizeof(vfs_inode_t));
    dev_root->flags = VFS_DIRECTORY;
    dev_root->iops = &devfs_ops;

    vfs_register_mount("dev", dev_root);
}
