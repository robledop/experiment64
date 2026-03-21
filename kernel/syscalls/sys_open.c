#include <syscall_common.h>

#include <mem/heap.h>
#include <sys/fcntl.h>
#include <status.h>

int sys_open(const char* path, int flags)
{
    if (!path)
        return -EINVAL;
    const bool want_write = (flags & O_WRONLY) || (flags & O_RDWR);
    char abs_path[PATH_MAX];
    int status = resolve_user_path_checked(path, abs_path, sizeof(abs_path), "sys_open path");
    if (status != 0)
        return status;
    vfs_inode_t* inode = vfs_resolve_path(abs_path);
    if (!inode && (flags & O_CREATE))
    {
        if (vfs_mknod(abs_path, VFS_FILE, 0) == 0)
            inode = vfs_resolve_path(abs_path);
    }
    if (!inode)
        return -ENOENT;

    if ((inode->flags & VFS_TYPE_MASK) == VFS_DIRECTORY && want_write)
    {
        vfs_release(inode);
        return -EISDIR;
    }

    // Initialize ref count for dup() support (atomic CAS for SMP safety)
    uint32_t expected_ref = 0;
    __atomic_compare_exchange_n(&inode->ref, &expected_ref, 1, false, __ATOMIC_ACQ_REL, __ATOMIC_RELAXED);

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

    file_descriptor_t* desc = fd_alloc(inode, flags);
    if (!desc)
    {
        vfs_release(inode);
        return -ENOMEM;
    }

    if (flags & O_APPEND)
        desc->offset = inode->size;
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
