#include <syscall_common.h>

int sys_close(int fd)
{
    if (fd < 0 || fd >= MAX_FDS)
        return -1;

    uint64_t flags;
    SPIN_LOCK_INT_SAVE(current_process->fd_lock, flags);
    file_descriptor_t *desc = current_process->fd_table[fd];
    if (!desc) {
        SPIN_UNLOCK_INT_RESTORE(current_process->fd_lock, flags);
        return -1;
    }
    current_process->fd_table[fd] = nullptr;
    SPIN_UNLOCK_INT_RESTORE(current_process->fd_lock, flags);

    fd_put(desc);
    return 0;
}
