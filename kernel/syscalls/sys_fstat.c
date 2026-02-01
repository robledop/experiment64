#include <syscall_common.h>

int sys_fstat(int fd, struct stat *st)
{
    if (!st || fd < 0 || fd >= MAX_FDS)
        return -1;
    if (!user_ptr_write_ok(st, sizeof(*st), "sys_fstat"))
        return -1;

    file_descriptor_t *desc = fd_get(fd);
    if (!desc)
        return -1;
    if (!desc->inode)
    {
        fd_put(desc);
        return -1;
    }

    fill_stat_from_inode(desc->inode, st);
    fd_put(desc);
    return 0;
}
