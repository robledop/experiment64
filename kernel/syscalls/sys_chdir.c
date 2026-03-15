#include <syscall_common.h>

#include <status.h>

int sys_chdir(const char *path)
{
    if (!path)
        return -EINVAL;
    char abs_path[PATH_MAX];
    int status = resolve_user_path_checked(path, abs_path, sizeof(abs_path), "sys_chdir path");
    if (status != 0)
        return status;

    vfs_inode_t *node = vfs_resolve_path(abs_path);
    if (!node)
        return -ENOENT;
    if ((node->flags & VFS_TYPE_MASK) != VFS_DIRECTORY) {
        vfs_release(node);
        return -ENOTDIR;
    }

    path_safe_copy(current_process->cwd, sizeof(current_process->cwd), abs_path);
    vfs_release(node);
    return ALL_OK;
}
