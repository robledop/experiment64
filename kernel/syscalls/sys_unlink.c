#include <syscall_common.h>
#include <lib/string.h>
#include <status.h>

int sys_unlink(const char* path)
{
    if (!path)
        return -EINVAL;
    if (!user_ptr_read_ok(path, 1, "sys_unlink path"))
        return -EFAULT;

    char abs_path[PATH_MAX];
    if (resolve_user_path(path, abs_path, sizeof(abs_path)) != 0)
        return -EBADPATH;

    if (strcmp(abs_path, "/") == 0)
        return -EPERM;

    vfs_inode_t *node = vfs_resolve_path(abs_path);
    if (!node)
        return -ENOENT;
    if ((node->flags & VFS_TYPE_MASK) == VFS_DIRECTORY) {
        vfs_release(node);
        return -EISDIR;
    }
    vfs_release(node);

    return vfs_unlink(abs_path);
}
