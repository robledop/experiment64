#include <syscall_common.h>

#include <status.h>

int sys_chdir(const char *path)
{
    if (!path)
        return -EINVAL;
    if (!user_ptr_read_ok(path, 1, "sys_chdir path"))
        return -EFAULT;
    char abs_path[PATH_MAX];
    if (resolve_user_path(path, abs_path, sizeof(abs_path)) != 0)
        return -EBADPATH;

    vfs_inode_t *node = vfs_resolve_path(abs_path);
    if (!node)
        return -ENOENT;
    if ((node->flags & 0x07) != VFS_DIRECTORY) {
        vfs_release(node);
        return -ENOTDIR;
    }

    path_safe_copy(current_process->cwd, sizeof(current_process->cwd), abs_path);
    vfs_release(node);
    return ALL_OK;
}
