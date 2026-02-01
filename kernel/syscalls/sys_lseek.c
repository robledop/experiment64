#include <syscall_common.h>

long sys_lseek(int fd, long offset, int whence)
{
    if (fd < 3 || fd >= MAX_FDS)
        return -1;
    file_descriptor_t* desc = fd_get(fd);
    if (!desc)
        return -1;
    if (!desc->inode)
    {
        fd_put(desc);
        return -1;
    }

    // Pipes are not seekable
    if (desc->inode->flags == VFS_PIPE)
    {
        fd_put(desc);
        return -1;
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
        return -1;
    }

    if (new_offset < 0)
    {
        fd_put(desc);
        return -1;
    }

    desc->offset = (uint64_t)new_offset;
    fd_put(desc);
    return new_offset;
}
