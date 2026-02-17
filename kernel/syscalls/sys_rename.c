#include <lib/string.h>
#include <status.h>
#include <syscall_common.h>

int sys_rename(const char *oldpath, const char *newpath)
{
    if (!oldpath || !newpath)
        return -EINVAL;
    if (!user_ptr_read_ok(oldpath, 1, "sys_rename oldpath"))
        return -EFAULT;
    if (!user_ptr_read_ok(newpath, 1, "sys_rename newpath"))
        return -EFAULT;

    char abs_old[PATH_MAX];
    char abs_new[PATH_MAX];
    if (resolve_user_path(oldpath, abs_old, sizeof(abs_old)) != 0)
        return -EBADPATH;
    if (resolve_user_path(newpath, abs_new, sizeof(abs_new)) != 0)
        return -EBADPATH;

    if (strcmp(abs_old, "/") == 0 || strcmp(abs_new, "/") == 0)
        return -EPERM;

    vfs_inode_t *old_node = vfs_resolve_path(abs_old);
    if (!old_node)
        return -ENOENT;
    if ((old_node->flags & 0x07) == VFS_DIRECTORY) {
        release_resolved_inode(old_node);
        return -EPERM;
    }
    release_resolved_inode(old_node);

    char new_parent_path[PATH_MAX];
    int split_status = split_parent_path(abs_new, new_parent_path, sizeof(new_parent_path));
    if (split_status != 0)
        return split_status;

    vfs_inode_t *new_parent = vfs_resolve_path(new_parent_path);
    if (!new_parent)
        return -ENOENT;
    if ((new_parent->flags & 0x07) != VFS_DIRECTORY) {
        release_resolved_inode(new_parent);
        return -ENOTDIR;
    }
    release_resolved_inode(new_parent);

    vfs_inode_t *new_node = vfs_resolve_path(abs_new);
    if (new_node) {
        if ((new_node->flags & 0x07) == VFS_DIRECTORY) {
            release_resolved_inode(new_node);
            return -EISDIR;
        }
        release_resolved_inode(new_node);
    }

    if (strcmp(abs_old, abs_new) == 0)
        return ALL_OK;

    return vfs_rename(abs_old, abs_new);
}
