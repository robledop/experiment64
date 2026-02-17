#include <syscall_common.h>
#include <status.h>

int sys_fstat(int fd, struct stat *st)
{
    if (fd < 0 || fd >= MAX_FDS)
        return -EBADF;
    if (!st)
        return -EINVAL;
    if (!user_ptr_write_ok(st, sizeof(*st), "sys_fstat"))
        return -EFAULT;

    file_descriptor_t *desc = fd_get(fd);
    if (!desc)
        return -EBADF;
    if (!desc->inode)
    {
        fd_put(desc);
        return -EBADF;
    }

    fill_stat_from_inode(desc->inode, st);
    fd_put(desc);
    return ALL_OK;
}
