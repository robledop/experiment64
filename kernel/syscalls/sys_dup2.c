#include <status.h>
#include <sys/syscall.h>
#include <syscall_common.h>

/**
 * Duplicate a file descriptor to a specific new fd, closing the new fd first if it is already open. If oldfd and newfd
 * are the same, dup2 does nothing and returns newfd. The new file descriptor shares the same underlying file
 * description as oldfd, and thus the same file offset and file status flags.
 * @param oldfd The file descriptor to duplicate.
 * @param newfd The target file descriptor.
 * @return The new file descriptor on success, or a negative error code on failure.
 */
int sys_dup2(int oldfd, int newfd)
{
    if (oldfd == newfd)
        return newfd;
    if (newfd < 0 || newfd >= MAX_FDS)
        return -EBADF;

    file_descriptor_t *old_desc = fd_get(oldfd);
    if (!old_desc)
        return -EBADF;

    sys_close(newfd);

    WITH_LOCK(current_process->fd_lock) {
        current_process->fd_table[newfd] = old_desc;
        __atomic_add_fetch(&old_desc->ref, 1, __ATOMIC_RELAXED);
    }

    fd_put(old_desc);
    return newfd;
}
