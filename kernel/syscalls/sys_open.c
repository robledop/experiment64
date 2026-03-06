#include <syscall_common.h>

#include <mem/heap.h>
#include <sys/fcntl.h>
#include <status.h>

int sys_open(const char* path, int flags)
{
    if (!path)
        return -EINVAL;
    if (!user_ptr_read_ok(path, 1, "sys_open path"))
        return -EFAULT;
    const bool want_write = (flags & O_WRONLY) || (flags & O_RDWR);
    char abs_path[PATH_MAX];
    if (resolve_user_path(path, abs_path, sizeof(abs_path)) != 0)
        return -EBADPATH;
    vfs_inode_t* inode = vfs_resolve_path(abs_path);
    if (!inode && (flags & O_CREATE))
    {
        if (vfs_mknod(abs_path, VFS_FILE, 0) == 0)
            inode = vfs_resolve_path(abs_path);
    }
    if (!inode)
        return -ENOENT;

    if ((inode->flags & 0x07) == VFS_DIRECTORY && want_write)
    {
        vfs_release(inode);
        return -EISDIR;
    }

    // Initialize ref count for dup() support
    if (inode->ref == 0)
        inode->ref = 1;

    if ((flags & O_TRUNC) && (inode->flags & VFS_FILE))
    {
        if (!want_write)
        {
            vfs_release(inode);
            return -EINVAL;
        }
        if (vfs_truncate(inode) != 0)
        {
            vfs_release(inode);
            return -EIO;
        }
    }

    file_descriptor_t* desc = kmalloc(sizeof(file_descriptor_t));
    if (!desc)
    {
        vfs_release(inode);
        return -ENOMEM;
    }

    desc->inode = inode;
    desc->offset = 0;
    if (flags & O_APPEND)
        desc->offset = inode->size;
    desc->flags = flags;
    desc->ref = 1;
    int fd = fd_assign(desc, 3);
    if (fd == -1)
    {
        kfree(desc);
        vfs_release(inode);
        return -EBUFFULL;
    }

    vfs_open(inode);
    return fd;
}
