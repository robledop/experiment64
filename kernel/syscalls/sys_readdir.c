#include <syscall_common.h>

#include <mem/heap.h>

int sys_readdir(int fd, vfs_dirent_t* dent)
{
    if (fd < 3 || fd >= MAX_FDS) return -1;
    file_descriptor_t* desc = fd_get(fd);
    if (!desc) return -1;

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
        return -1;
    }
    kfree(d);
    desc->offset++;
    fd_put(desc);
    return 1; // Success
}
