#include <status.h>
#include <syscall_common.h>

/**
 * Duplicate a file descriptor, returning the new file descriptor. The new file descriptor shares the same underlying
 * file description as oldfd, and thus the same file offset and file status flags. The new file descriptor is guaranteed
 * to be the lowest-numbered file descriptor that is not already open for the process
 * @param oldfd The file descriptor to duplicate.
 * @return The new file descriptor on success, or a negative error code on failure. 
 */
int sys_dup(const int oldfd)
{
    file_descriptor_t *old_desc = fd_get(oldfd);
    if (!old_desc)
        return -EBADF;

    // Find the lowest available fd (per POSIX, starts from 0)
    int newfd = fd_assign(old_desc, 0);
    if (newfd == -1) {
        fd_put(old_desc);
        return -EBUFFULL;
    }

    __atomic_add_fetch(&old_desc->ref, 1, __ATOMIC_RELAXED);
    fd_put(old_desc);
    return newfd;
}
