#include "vfs.h"
#include "keyboard.h"
#include "terminal.h"
#include "string.h"
#include "heap.h"
#include "console.h"
#include "devfs.h"
#include "ioctl.h"

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

static int console_ioctl([[maybe_unused]] vfs_inode_t *node, int request, void *arg)
{
    if (request == TIOCGWINSZ)
    {
        if (!arg)
            return -1;
        int cols = 0, rows = 0, width = 0, height = 0;
        terminal_get_dimensions(&cols, &rows);
        terminal_get_resolution(&width, &height);

        struct winsize ws = {
            .ws_row = (uint16_t)rows,
            .ws_col = (uint16_t)cols,
            .ws_xpixel = (uint16_t)width,
            .ws_ypixel = (uint16_t)height,
        };
        memcpy(arg, &ws, sizeof(ws));
        return 0;
    }
    return -1;
}

struct inode_operations console_ops = {
    .read = console_read,
    .write = console_write,
    .ioctl = console_ioctl,
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
