#include <syscall_common.h>
#include <status.h>

int sys_ftruncate(int fd, long length)
{
    if (fd < 0 || fd >= MAX_FDS)
        return -EBADF;
    if (length < 0)
        return -EINVAL;

    file_descriptor_t *desc = fd_get(fd);
    if (!desc)
        return -EBADF;
    if (!desc->inode || !fd_can_write(desc)) {
        fd_put(desc);
        return -EBADF;
    }

    const uint32_t inode_type = desc->inode->flags & 0x07;
    if (inode_type == VFS_DIRECTORY) {
        fd_put(desc);
        return -EISDIR;
    }
    if (inode_type != VFS_FILE) {
        fd_put(desc);
        return -ENOTSUP;
    }

    if ((uint64_t)length == desc->inode->size) {
        fd_put(desc);
        return 0;
    }

    if (length != 0) {
        fd_put(desc);
        return -ENOTSUP;
    }

    if (vfs_truncate(desc->inode) != 0) {
        fd_put(desc);
        return -EIO;
    }

    if (desc->offset > (uint64_t)length)
        desc->offset = (uint64_t)length;

    fd_put(desc);
    return 0;
}
