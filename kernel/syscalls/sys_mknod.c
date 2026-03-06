#include <syscall_common.h>
#include <status.h>

int sys_mknod(const char* path, int mode, int dev)
{
    if (!path)
        return -EINVAL;
    if (!user_ptr_read_ok(path, 1, "sys_mknod path"))
        return -EFAULT;
    if (mode != VFS_FILE &&
        mode != VFS_DIRECTORY &&
        mode != VFS_CHARDEVICE &&
        mode != VFS_BLOCKDEVICE)
        return -EINVAL;

    char abs_path[PATH_MAX];
    if (resolve_user_path(path, abs_path, sizeof(abs_path)) != 0)
        return -EBADPATH;

    if (strcmp(abs_path, "/") == 0)
        return -EPERM;

    vfs_inode_t *existing = vfs_resolve_path(abs_path);
    if (existing) {
        vfs_release(existing);
        return -EINSTKN;
    }

    char parent_path[PATH_MAX];
    int split_status = split_parent_path(abs_path, parent_path, sizeof(parent_path));
    if (split_status != 0)
        return split_status;

    vfs_inode_t *parent = vfs_resolve_path(parent_path);
    if (!parent)
        return -ENOENT;
    if ((parent->flags & 0x07) != VFS_DIRECTORY) {
        vfs_release(parent);
        return -ENOTDIR;
    }
    vfs_release(parent);

    return vfs_mknod(abs_path, mode, dev);
}
