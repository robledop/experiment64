#include <syscall_common.h>

#include <status.h>

int sys_stat(const char* path, struct stat* st)
{
    if (!path || !st)
        return -EINVAL;
    int status = require_user_ptr_write(st, sizeof(*st), "sys_stat", -EFAULT);
    if (status != 0)
        return status;

    char abs_path[PATH_MAX];
    status = resolve_user_path_checked(path, abs_path, sizeof(abs_path), "sys_stat path");
    if (status != 0)
        return status;

    vfs_inode_t* inode = vfs_resolve_path(abs_path);
    if (!inode)
        return -ENOENT;

    fill_stat_from_inode(inode, st);
    vfs_release(inode);
    return ALL_OK;
}
