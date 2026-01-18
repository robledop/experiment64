#include "syscall_common.h"

#include <drivers/terminal.h>
#include <fs/vfs.h>
#include <lib/util.h>
#include <sys/fcntl.h>

int sys_write(int fd, const char* buf, size_t count)
{
    if (fd < 0 || fd >= MAX_FDS)
        return -1;

    file_descriptor_t* desc = current_process->fd_table[fd];

    // Handle stdout/stderr (fd 1/2) specially - check if redirected to pipe/file
    if (fd == 1 || fd == 2)
    {
        // If fd has a real descriptor with an inode, write to it (pipe or file redirection)
        if (desc && desc->inode)
        {
            if (!fd_can_write(desc))
                return -1;
            if (desc->flags & O_APPEND)
                desc->offset = desc->inode->size;
            uint64_t written = vfs_write(desc->inode, desc->offset, count, (uint8_t*)buf);
            desc->offset += written;
            return clamp_to_int(written);
        }

        // No inode - write to terminal (console device)
        if (desc && !fd_can_write(desc))
            return -1;

        terminal_write(buf, count);
        return clamp_to_int(count);
    }

    if (!desc || !desc->inode || !fd_can_write(desc))
        return -1;

    if (desc->flags & O_APPEND)
        desc->offset = desc->inode->size;

    uint64_t written = vfs_write(desc->inode, desc->offset, count, (uint8_t*)buf);
    desc->offset += written;
    return clamp_to_int(written);
}
