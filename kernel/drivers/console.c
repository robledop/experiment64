#include <fs/vfs.h>
#include <drivers/keyboard.h>
#include <drivers/terminal.h>
#include <drivers/uart.h>
#include <drivers/tsc.h>
#include <lib/string.h>
#include <mem/heap.h>
#include <drivers/console.h>
#include <fs/devfs.h>
#include <sys/ioctl.h>
#include <sys/poll.h>
#include <sys/termios.h>
#include <syscall_common.h>
#include <task/process.h>

static struct termios console_termios = {
    .c_iflag = IXON | ICRNL,
    .c_oflag = OPOST,
    .c_cflag = 0,
    .c_lflag = ECHO | ICANON,
    .c_cc = {[VMIN] = 1, [VTIME] = 0},
};
static int console_nonblock = 0;

static bool console_try_get_char(char *out)
{
    if (!out)
        return false;

    if (keyboard_try_get_char(out))
        return true;

    if (uart_try_getc(out)) {
        if (*out == '\r')
            *out = '\n';
        else if (*out == 0x7F)
            *out = '\b';
        return true;
    }
    return false;
}

bool console_has_char(void)
{
    return keyboard_has_char() || uart_has_rx();
}

char console_get_char(void)
{
    while (1) {
        char c = 0;
        if (console_try_get_char(&c))
            return c;

        if (scheduler_is_ready() && get_current_thread())
            schedule();
        else
            __asm__ volatile("pause");
    }
}

uint64_t console_read([[maybe_unused]] const vfs_inode_t *node, [[maybe_unused]] uint64_t offset, uint64_t size,
                      uint8_t *buffer)
{
    if (!buffer || size == 0)
        return 0;

    const uint32_t vmin = console_termios.c_cc[VMIN] > size ? (uint32_t)size : console_termios.c_cc[VMIN];
    const uint32_t vtime = console_termios.c_cc[VTIME];
    const int nonblock = console_nonblock;
    const uint64_t timeout_ns = (uint64_t)vtime * 100000000ull;

    uint64_t bytes_read = 0;
    uint64_t deadline_ns = 0;
    bool deadline_active = false;

    while (bytes_read < size) {
        char c = 0;
        if (console_try_get_char(&c)) {
            buffer[bytes_read++] = (uint8_t)c;
            if (vmin == 0)
                break;
            if (vtime > 0) {
                deadline_ns = tsc_monotonic_ns() + timeout_ns;
                deadline_active = true;
            }
            if (bytes_read >= vmin)
                break;
            continue;
        }

        if (bytes_read > 0) {
            if (nonblock)
                break;
            if (vmin == 0)
                break;
            if (vtime > 0 && deadline_active && tsc_monotonic_ns() >= deadline_ns)
                break;
        } else {
            if (nonblock)
                break;
            if (vmin == 0) {
                if (vtime == 0)
                    break;
                if (!deadline_active) {
                    deadline_ns = tsc_monotonic_ns() + timeout_ns;
                    deadline_active = true;
                } else if (tsc_monotonic_ns() >= deadline_ns) {
                    break;
                }
            }
        }

        schedule();
    }

    return bytes_read;
}

uint64_t console_write([[maybe_unused]] vfs_inode_t *node, [[maybe_unused]] uint64_t offset, uint64_t size,
                       uint8_t *buffer)
{
    terminal_write((char *)buffer, size);
    return size;
}

static int console_ioctl([[maybe_unused]] vfs_inode_t *node, int request, void *arg)
{
    if (request == TIOCGWINSZ) {
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
        copy_to_user(arg, &ws, sizeof(ws));
        return 0;
    }
    if (request == TIOCGETA) {
        if (!arg)
            return -1;
        copy_to_user(arg, &console_termios, sizeof(console_termios));
        return 0;
    }
    if (request == TIOCSETA) {
        if (!arg)
            return -1;
        struct termios t = {0};
        copy_from_user(&t, arg, sizeof(t));
        console_termios = t;
        return 0;
    }
    if (request == TIOCSPGRP) {
        if (!arg)
            return -1;
        int pid = 0;
        copy_from_user(&pid, arg, sizeof(pid));
        keyboard_set_foreground_pid(pid);
        return 0;
    }
    if (request == TIOCGPGRP) {
        if (!arg)
            return -1;
        int pid = keyboard_get_foreground_pid();
        copy_to_user(arg, &pid, sizeof(pid));
        return 0;
    }
    if (request == FIONBIO) {
        if (!arg)
            return -1;
        int val = 0;
        copy_from_user(&val, arg, sizeof(val));
        console_nonblock = val ? 1 : 0;
        return 0;
    }
    return -1;
}

static int console_poll([[maybe_unused]] const vfs_inode_t *node, short events, short *revents)
{
    if (!revents)
        return -1;

    short out = 0;
    if ((events & (POLLIN | POLLPRI)) && console_has_char())
        out |= POLLIN;
    if (events & POLLOUT)
        out |= POLLOUT;

    *revents = out;
    return 0;
}

struct inode_operations console_ops = {
    .read = console_read,
    .write = console_write,
    .ioctl = console_ioctl,
    .poll = console_poll,
};

vfs_inode_t *console_device = nullptr;

void console_init()
{
    console_device = kmalloc(sizeof(vfs_inode_t));
    if (!console_device)
        return;
    memset(console_device, 0, sizeof(vfs_inode_t));
    console_device->flags = VFS_CHARDEVICE;
    console_device->iops  = &console_ops;

    devfs_register_device("console", console_device);
}
