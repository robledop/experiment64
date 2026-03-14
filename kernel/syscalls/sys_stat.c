#include <syscall_common.h>

#include <status.h>

int sys_stat(const char* path, struct stat* st)
{
    if (!path || !st)
        return -EINVAL;
    if (!user_ptr_read_ok(path, 1, "sys_stat path"))
        return -EFAULT;
    if (!user_ptr_write_ok(st, sizeof(*st), "sys_stat"))
        return -EFAULT;

    char abs_path[PATH_MAX];
    if (resolve_user_path(path, abs_path, sizeof(abs_path)) != 0)
        return -EBADPATH;

    vfs_inode_t* inode = vfs_resolve_path(abs_path);
    if (!inode)
        return -ENOENT;

    struct stat kst;
    fill_stat_from_inode(inode, &kst);
    vfs_release(inode);
    if (!copy_to_user(st, &kst, sizeof(kst)))
        return -EFAULT;
    return ALL_OK;
}
