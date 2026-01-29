#include <syscall_common.h>

#include <mem/heap.h>
#include <sys/fcntl.h>

int sys_open(const char* path, int flags)
{
    if (!path)
        return -1;
    const bool want_write = (flags & O_WRONLY) || (flags & O_RDWR);
    char abs_path[PATH_MAX];
    if (resolve_user_path(path, abs_path, sizeof(abs_path)) != 0)
        return -1;
    int fd = -1;
    for (int i = 3; i < MAX_FDS; i++)
    {
        if (current_process->fd_table[i] == nullptr)
        {
            fd = i;
            break;
        }
    }
    if (fd == -1)
        return -1;

    vfs_inode_t* inode = vfs_resolve_path(abs_path);
    if (!inode && (flags & O_CREATE))
    {
        if (vfs_mknod(abs_path, VFS_FILE, 0) == 0)
            inode = vfs_resolve_path(abs_path);
    }
    if (!inode)
        return -1;

    // Initialize ref count for dup() support
    if (inode->ref == 0)
        inode->ref = 1;

    if ((flags & O_TRUNC) && (inode->flags & VFS_FILE))
    {
        if (!want_write)
        {
            vfs_close(inode);
            if (inode != vfs_root)
                kfree(inode);
            return -1;
        }
        if (vfs_truncate(inode) != 0)
        {
            vfs_close(inode);
            if (inode != vfs_root)
                kfree(inode);
            return -1;
        }
    }

    file_descriptor_t* desc = kmalloc(sizeof(file_descriptor_t));
    if (!desc)
    {
        vfs_close(inode);
        if (inode != vfs_root)
            kfree(inode);
        return -1;
    }

    desc->inode = inode;
    desc->offset = 0;
    if (flags & O_APPEND)
        desc->offset = inode->size;
    desc->flags = flags;
    desc->ref = 1;
    current_process->fd_table[fd] = desc;

    vfs_open(inode);
    return fd;
}
