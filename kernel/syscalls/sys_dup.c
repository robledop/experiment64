#include <lib/string.h>
#include <task/process.h>

int sys_dup(int oldfd)
{
    if (oldfd < 0 || oldfd >= MAX_FDS)
        return -1;
    file_descriptor_t* old_desc = current_process->fd_table[oldfd];
    if (!old_desc)
        return -1;

    // Find the lowest available fd (per POSIX, starts from 0)
    int newfd = -1;
    for (int i = 0; i < MAX_FDS; i++)
    {
        if (current_process->fd_table[i] == nullptr)
        {
            newfd = i;
            break;
        }
    }
    if (newfd == -1)
        return -1;

    // Share the file descriptor (both fds point to same descriptor)
    // This ensures they share the same file offset per POSIX semantics
    old_desc->ref++;
    current_process->fd_table[newfd] = old_desc;
    return newfd;
}
