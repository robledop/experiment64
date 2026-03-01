#include <syscall_common.h>

#include <status.h>
#include <sys/fcntl.h>
#include <sys/ioctl.h>

int sys_fcntl(int fd, int cmd, long arg)
{
    if (fd < 0 || fd >= MAX_FDS)
        return -EBADF;

    file_descriptor_t *desc = fd_get(fd);
    if (!desc)
        return -EBADF;
    if (!desc->inode) {
        fd_put(desc);
        return -EBADF;
    }

    int rc = 0;
    switch (cmd) {
    case F_GETFL:
        rc = desc->flags;
        break;
    case F_SETFL: {
        constexpr int mutable_flags = O_APPEND | O_NONBLOCK;
        const int requested_flags   = (int)arg;
        desc->flags                 = (desc->flags & ~mutable_flags) | (requested_flags & mutable_flags);

        int nonblock = (desc->flags & O_NONBLOCK) ? 1 : 0;
        (void)vfs_ioctl(desc->inode, FIONBIO, &nonblock);
        rc = 0;
        break;
    }
    case F_GETFD:
        rc = 0;
        break;
    case F_SETFD:
        if (((int)arg & ~FD_CLOEXEC) != 0)
            rc = -EINVAL;
        else
            rc = 0;
        break;
    default:
        rc = -EINVAL;
        break;
    }

    fd_put(desc);
    return rc;
}
