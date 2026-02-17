#include <syscall_common.h>

#include <mem/heap.h>
#include <status.h>

int sys_readdir(int fd, vfs_dirent_t* dent)
{
    if (fd < 3 || fd >= MAX_FDS)
        return -EBADF;
    if (!user_ptr_write_ok(dent, sizeof(vfs_dirent_t), "sys_readdir"))
        return -EFAULT;
    file_descriptor_t* desc = fd_get(fd);
    if (!desc)
        return -EBADF;
    if (!desc->inode)
    {
        fd_put(desc);
        return -EBADF;
    }
    if ((desc->inode->flags & 0x07) != VFS_DIRECTORY)
    {
        fd_put(desc);
        return -ENOTDIR;
    }

    vfs_dirent_t* d = vfs_readdir(desc->inode, desc->offset);
    if (!d)
    {
        fd_put(desc);
        return 0; // End of directory
    }

    if (!copy_to_user(dent, d, sizeof(vfs_dirent_t)))
    {
        kfree(d);
        fd_put(desc);
        return -EFAULT;
    }
    kfree(d);
    desc->offset++;
    fd_put(desc);
    return 1; // Success
}
