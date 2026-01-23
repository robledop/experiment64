#include <fs/vfs.h>
#include <task/process.h>

int sys_ioctl(int fd, int request, void* arg)
{
    if (fd < 0 || fd >= MAX_FDS) return -1;
    file_descriptor_t* desc = current_process->fd_table[fd];
    if (!desc || !desc->inode) return -1;

    return vfs_ioctl(desc->inode, request, arg);
}
