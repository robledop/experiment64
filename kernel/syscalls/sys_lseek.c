#include <syscall_common.h>
#include <status.h>

long sys_lseek(int fd, long offset, int whence)
{
    if (fd < 3 || fd >= MAX_FDS)
        return -EBADF;
    file_descriptor_t* desc = fd_get(fd);
    if (!desc)
        return -EBADF;
    if (!desc->inode)
    {
        fd_put(desc);
        return -EBADF;
    }

    // Pipes are not seekable
    if (desc->inode->flags == VFS_PIPE)
    {
        fd_put(desc);
        return -ENOTSUP;
    }

    long new_offset;
    switch (whence)
    {
    case 0: // SEEK_SET
        new_offset = offset;
        break;
    case 1: // SEEK_CUR
        new_offset = (long)desc->offset + offset;
        break;
    case 2: // SEEK_END
        new_offset = (long)desc->inode->size + offset;
        break;
    default:
        fd_put(desc);
        return -EINVAL;
    }

    if (new_offset < 0)
    {
        fd_put(desc);
        return -EINVAL;
    }

    desc->offset = (uint64_t)new_offset;
    fd_put(desc);
    return new_offset;
}
