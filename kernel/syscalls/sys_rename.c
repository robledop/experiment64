#include <lib/string.h>
#include <status.h>
#include <syscall_common.h>

int sys_rename(const char *oldpath, const char *newpath)
{
    if (!oldpath || !newpath)
        return -EINVAL;

    char abs_old[PATH_MAX];
    char abs_new[PATH_MAX];
    int status = resolve_user_path_checked(oldpath, abs_old, sizeof(abs_old), "sys_rename oldpath");
    if (status != 0)
        return status;
    status = resolve_user_path_checked(newpath, abs_new, sizeof(abs_new), "sys_rename newpath");
    if (status != 0)
        return status;

    if (strcmp(abs_old, "/") == 0 || strcmp(abs_new, "/") == 0)
        return -EPERM;

    vfs_inode_t *old_node = vfs_resolve_path(abs_old);
    if (!old_node)
        return -ENOENT;
    if ((old_node->flags & VFS_TYPE_MASK) == VFS_DIRECTORY) {
        vfs_release(old_node);
        return -EPERM;
    }
    vfs_release(old_node);

    char new_parent_path[PATH_MAX];
    int split_status = split_parent_path(abs_new, new_parent_path, sizeof(new_parent_path));
    if (split_status != 0)
        return split_status;

    vfs_inode_t *new_parent = vfs_resolve_path(new_parent_path);
    if (!new_parent)
        return -ENOENT;
    if ((new_parent->flags & VFS_TYPE_MASK) != VFS_DIRECTORY) {
        vfs_release(new_parent);
        return -ENOTDIR;
    }
    vfs_release(new_parent);

    vfs_inode_t *new_node = vfs_resolve_path(abs_new);
    if (new_node) {
        if ((new_node->flags & VFS_TYPE_MASK) == VFS_DIRECTORY) {
            vfs_release(new_node);
            return -EISDIR;
        }
        vfs_release(new_node);
    }

    if (strcmp(abs_old, abs_new) == 0)
        return ALL_OK;

    return vfs_rename(abs_old, abs_new);
}
