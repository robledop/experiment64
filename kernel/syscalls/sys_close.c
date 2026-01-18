#include <lib/string.h>
#include <mem/heap.h>
#include <task/process.h>

int sys_close(int fd)
{
    if (fd < 0 || fd >= MAX_FDS)
        return -1;
    file_descriptor_t* desc = current_process->fd_table[fd];
    if (!desc)
        return -1;

    current_process->fd_table[fd] = nullptr;

    if (desc->ref > 1)
    {
        desc->ref--;
        return 0; // Other fds still reference this descriptor
    }

    // Last reference to this descriptor - close the inode
    if (desc->inode && desc->inode != vfs_root)
    {
        // Only close and free inode when its ref count reaches 0
        if (desc->inode->ref <= 1)
        {
            vfs_close(desc->inode);
            kfree(desc->inode);
        }
        else
        {
            desc->inode->ref--;
        }
    }
    kfree(desc);
    return 0;
}
