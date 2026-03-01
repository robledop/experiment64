#include <syscall_common.h>

#include <drivers/tsc.h>
#include <mem/heap.h>
#include <status.h>
#include <sys/poll.h>

static short poll_regular_fallback(const file_descriptor_t *desc, short events)
{
    if (!desc || !desc->inode)
        return 0;

    const uint32_t inode_type = desc->inode->flags & 0x07;
    if (inode_type != VFS_FILE && inode_type != VFS_DIRECTORY && inode_type != VFS_BLOCKDEVICE && inode_type != VFS_SYMLINK)
        return 0;

    short revents = 0;
    if ((events & (POLLIN | POLLPRI)) && fd_can_read(desc))
        revents |= POLLIN;
    if ((events & POLLOUT) && fd_can_write(desc))
        revents |= POLLOUT;
    return revents;
}

static int poll_scan_fds(struct pollfd *fds, nfds_t nfds)
{
    int ready_count = 0;

    for (nfds_t i = 0; i < nfds; i++) {
        struct pollfd *pfd = &fds[i];
        pfd->revents       = 0;

        if (pfd->fd < 0)
            continue;

        file_descriptor_t *desc = fd_get(pfd->fd);
        if (!desc || !desc->inode) {
            if (desc)
                fd_put(desc);
            pfd->revents = POLLNVAL;
            ready_count++;
            continue;
        }

        short revents = 0;
        if (vfs_poll(desc->inode, pfd->events, &revents) != 0)
            revents = (short)(revents | poll_regular_fallback(desc, pfd->events));

        pfd->revents = revents;
        if (revents != 0)
            ready_count++;

        fd_put(desc);
    }

    return ready_count;
}

int sys_poll(struct pollfd *fds, long nfds, int timeout)
{
    if (nfds < 0 || timeout < -1)
        return -EINVAL;

    if (nfds == 0) {
        if (timeout == 0)
            return 0;
        if (timeout < 0) {
            while (1)
                schedule();
        }
        const uint64_t deadline = tsc_monotonic_ns() + (uint64_t)timeout * 1000000ull;
        while (tsc_monotonic_ns() < deadline)
            schedule();
        return 0;
    }

    if (!fds)
        return -EFAULT;
    if ((uint64_t)nfds > (SIZE_MAX / sizeof(struct pollfd)))
        return -EINVAL;

    const size_t bytes = (size_t)nfds * sizeof(struct pollfd);
    if (!user_ptr_read_ok(fds, bytes, "sys_poll read"))
        return -EFAULT;
    if (!user_ptr_write_ok(fds, bytes, "sys_poll write"))
        return -EFAULT;

    struct pollfd *kernel_fds = kmalloc(bytes);
    if (!kernel_fds)
        return -ENOMEM;

    if (!copy_from_user(kernel_fds, fds, bytes)) {
        kfree(kernel_fds);
        return -EFAULT;
    }

    const bool has_deadline = timeout > 0;
    const uint64_t deadline = has_deadline ? (tsc_monotonic_ns() + (uint64_t)timeout * 1000000ull) : 0;

    while (1) {
        int ready_count = poll_scan_fds(kernel_fds, (nfds_t)nfds);
        if (ready_count > 0) {
            if (!copy_to_user(fds, kernel_fds, bytes)) {
                kfree(kernel_fds);
                return -EFAULT;
            }
            kfree(kernel_fds);
            return ready_count;
        }

        if (timeout == 0) {
            if (!copy_to_user(fds, kernel_fds, bytes)) {
                kfree(kernel_fds);
                return -EFAULT;
            }
            kfree(kernel_fds);
            return 0;
        }

        if (has_deadline && tsc_monotonic_ns() >= deadline) {
            if (!copy_to_user(fds, kernel_fds, bytes)) {
                kfree(kernel_fds);
                return -EFAULT;
            }
            kfree(kernel_fds);
            return 0;
        }

        schedule();
    }
}
