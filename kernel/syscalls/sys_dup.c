#include <syscall_common.h>
#include <status.h>

int sys_dup(int oldfd)
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
