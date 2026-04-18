#include <syscall_common.h>
#include <status.h>

int sys_close(int fd)
{
    if (!current_process)
        return -EPERM;
    if (fd < 0 || fd >= MAX_FDS)
        return -EBADF;

    file_descriptor_t *desc = nullptr;
    WITH_LOCK(current_process->fd_lock) {
        desc = current_process->fd_table[fd];
        if (desc)
            current_process->fd_table[fd] = nullptr;
    }
    if (!desc)
        return -EBADF;

    fd_put(desc);
    return 0;
}
