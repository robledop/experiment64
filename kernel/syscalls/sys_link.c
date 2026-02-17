#include <syscall_common.h>
#include <status.h>

int sys_link(const char* oldpath, const char* newpath)
{
    if (!oldpath || !newpath)
        return -EINVAL;
    if (!user_ptr_read_ok(oldpath, 1, "sys_link oldpath"))
        return -EFAULT;
    if (!user_ptr_read_ok(newpath, 1, "sys_link newpath"))
        return -EFAULT;

    char abs_old[PATH_MAX];
    char abs_new[PATH_MAX];
    if (resolve_user_path(oldpath, abs_old, sizeof(abs_old)) != 0)
        return -EBADPATH;
    if (resolve_user_path(newpath, abs_new, sizeof(abs_new)) != 0)
        return -EBADPATH;
    if (strcmp(abs_new, "/") == 0)
        return -EPERM;

    vfs_inode_t *target = vfs_resolve_path(abs_old);
    if (!target)
        return -ENOENT;
    if ((target->flags & 0x07) == VFS_DIRECTORY) {
        release_resolved_inode(target);
        return -EPERM;
    }

    vfs_inode_t *existing = vfs_resolve_path(abs_new);
    if (existing) {
        release_resolved_inode(existing);
        release_resolved_inode(target);
        return -EINSTKN;
    }

    char new_parent_path[PATH_MAX];
    int split_status = split_parent_path(abs_new, new_parent_path, sizeof(new_parent_path));
    if (split_status != 0) {
        release_resolved_inode(target);
        return split_status;
    }

    vfs_inode_t *new_parent = vfs_resolve_path(new_parent_path);
    if (!new_parent) {
        release_resolved_inode(target);
        return -ENOENT;
    }
    if ((new_parent->flags & 0x07) != VFS_DIRECTORY) {
        release_resolved_inode(new_parent);
        release_resolved_inode(target);
        return -ENOTDIR;
    }
    release_resolved_inode(new_parent);
    release_resolved_inode(target);

    return vfs_link(abs_old, abs_new);
}
