#include <syscall_common.h>

#include <fs/vfs.h>
#include <net/network.h>
#include <sys/ioctl.h>
#include <task/process.h>

static size_t ioctl_arg_size(int request)
{
    switch (request)
    {
    case TIOCGWINSZ:
        return sizeof(struct winsize);
    case TIOCSPGRP:
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

int sys_ioctl(int fd, int request, void* arg)
{
    if (fd < 0 || fd >= MAX_FDS) return -1;
    file_descriptor_t* desc = current_process->fd_table[fd];
    if (!desc || !desc->inode) return -1;

    if (request != KDFLUSH)
    {
        size_t arg_size = ioctl_arg_size(request);
        if (arg_size == 0 || !arg)
            return -1;
        if (!user_ptr_write_ok(arg, arg_size, "sys_ioctl"))
            return -1;
    }

    return vfs_ioctl(desc->inode, request, arg);
}