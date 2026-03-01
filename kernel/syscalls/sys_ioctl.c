#include <syscall_common.h>

#include <fs/vfs.h>
#include <net/network.h>
#include <sys/ioctl.h>
#include <sys/termios.h>
#include <task/process.h>

typedef enum { IOCTL_DIR_READ, IOCTL_DIR_WRITE } ioctl_dir_t;

static size_t ioctl_arg_size(int request)
{
    switch (request) {
    case TIOCGWINSZ:
    case TIOCSWINSZ:
        return sizeof(struct winsize);
    case TIOCGETA:
    case TIOCSETA:
    case TCSETSW:
    case TCSETSF:
        return sizeof(struct termios);
    case TIOCGPGRP:
    case TIOCSPGRP:
    case FIONBIO:
        return sizeof(int);
    case FB_IOCTL_GET_WIDTH:
    case FB_IOCTL_GET_HEIGHT:
    case FB_IOCTL_GET_PITCH:
        return sizeof(uint32_t);
    case FB_IOCTL_GET_FBADDR:
        return sizeof(uint64_t);
    case GETNETINFO:
        return sizeof(struct netinfo);
    default:
        return 0;
    }
}

// Returns the direction of data flow for the arg pointer:
// IOCTL_DIR_WRITE = kernel writes to user (GET operations)
// IOCTL_DIR_READ  = kernel reads from user (SET operations)
static ioctl_dir_t ioctl_arg_dir(int request)
{
    switch (request) {
    case TIOCSWINSZ:
    case TIOCSETA:
    case TCSETSW:
    case TCSETSF:
    case TIOCSPGRP:
    case FIONBIO:
        return IOCTL_DIR_READ;
    default:
        return IOCTL_DIR_WRITE;
    }
}

int sys_ioctl(int fd, int request, void *arg)
{
    if (fd < 0 || fd >= MAX_FDS)
        return -1;
    file_descriptor_t *desc = fd_get(fd);
    if (!desc)
        return -1;
    if (!desc->inode) {
        fd_put(desc);
        return -1;
    }

    if (request != KDFLUSH && request != TIOCHUP) {
        size_t arg_size = ioctl_arg_size(request);
        if (arg_size == 0 || !arg) {
            fd_put(desc);
            return -1;
        }
        bool ok = (ioctl_arg_dir(request) == IOCTL_DIR_READ)
            ? user_ptr_read_ok(arg, arg_size, "sys_ioctl")
            : user_ptr_write_ok(arg, arg_size, "sys_ioctl");
        if (!ok) {
            fd_put(desc);
            return -1;
        }
    }

    int res = vfs_ioctl(desc->inode, request, arg);
    fd_put(desc);
    return res;
}
