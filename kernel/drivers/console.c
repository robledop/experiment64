#include "vfs.h"
#include "keyboard.h"
#include "terminal.h"
#include "string.h"
#include "heap.h"
#include "console.h"
#include "devfs.h"

uint64_t console_read([[maybe_unused]] const vfs_inode_t *node, [[maybe_unused]] uint64_t offset, uint64_t size, uint8_t *buffer)
{
    for (uint64_t i = 0; i < size; i++)
    {
        buffer[i] = keyboard_get_char();
    }
    return size;
}

uint64_t console_write([[maybe_unused]] vfs_inode_t *node, [[maybe_unused]] uint64_t offset, uint64_t size, uint8_t *buffer)
{
    terminal_write((char *)buffer, size);
    return size;
}

struct inode_operations console_ops = {
    .read = console_read,
    .write = console_write,
};

vfs_inode_t *console_device = nullptr;

void console_init()
{
    console_device = kmalloc(sizeof(vfs_inode_t));
    memset(console_device, 0, sizeof(vfs_inode_t));
    console_device->flags = VFS_CHARDEVICE;
    console_device->iops = &console_ops;

    devfs_register_device("console", console_device);
}
