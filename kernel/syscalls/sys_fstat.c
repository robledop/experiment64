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

    struct stat kst;
    fill_stat_from_inode(desc->inode, &kst);
    fd_put(desc);
    if (!copy_to_user(st, &kst, sizeof(kst)))
        return -EFAULT;
    return ALL_OK;
}
