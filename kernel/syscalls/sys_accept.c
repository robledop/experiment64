#include <syscall_common.h>
#include <lib/string.h>
#include <mem/heap.h>
#include <net/socket.h>
#include <sys/fcntl.h>
#include <status.h>

int sys_close(int fd);

int sys_accept(const int fd, struct sockaddr* addr, socklen_t* addrlen)
{
    if (fd < 0 || fd >= MAX_FDS)
        return -EBADF;

    file_descriptor_t* desc = fd_get(fd);
    if (!desc)
        return -EBADF;
    if (!desc->inode)
    {
        fd_put(desc);
        return -EBADF;
    }
    if (desc->inode->iops != &socket_iops)
    {
        fd_put(desc);
        return -ENOTSUP;
    }
    const bool nonblock = (desc->flags & O_NONBLOCK) != 0;

    auto listener = (socket_t*)desc->inode->device;
    if (!listener)
    {
        fd_put(desc);
        return -EIO;
    }
    socket_hold(listener);
    fd_put(desc);
    if (listener->type != SOCK_STREAM || listener->protocol != IPPROTO_TCP)
    {
        socket_put(listener);
        return -ENOTSUP;
    }
    if (listener->state != SOCKET_STATE_LISTENING)
    {
        socket_put(listener);
        return -EINVAL;
    }
    const socklen_t actual_len = sizeof(struct sockaddr_in);
    socklen_t caller_len       = 0;
    if (addr)
    {
        if (!addrlen)
        {
            socket_put(listener);
            return -EFAULT;
        }
        if (!user_ptr_read_ok(addrlen, sizeof(*addrlen), "sys_accept addrlen read") ||
            !user_ptr_write_ok(addrlen, sizeof(*addrlen), "sys_accept addrlen write"))
        {
            socket_put(listener);
            return -EFAULT;
        }
        if (!copy_from_user(&caller_len, addrlen, sizeof(caller_len)))
        {
            socket_put(listener);
            return -EFAULT;
        }
        if (caller_len > 0 && !user_ptr_write_ok(addr, caller_len, "sys_accept addr"))
        {
            socket_put(listener);
            return -EFAULT;
        }
    }

    socket_t* child = nullptr;
    uint64_t rflags;
    SPIN_LOCK_INT_SAVE(listener->accept_lock, rflags);
    while (list_empty(&listener->accept_queue)) {
        if (nonblock) {
            SPIN_UNLOCK_INT_RESTORE(listener->accept_lock, rflags);
            socket_put(listener);
            return -EAGAIN;
        }
        thread_sleep(listener, &listener->accept_lock);
    }
    child = list_entry(listener->accept_queue.next, socket_t, accept_list);
    list_del(&child->accept_list);
    if (listener->accept_queue_len > 0)
        listener->accept_queue_len--;
    SPIN_UNLOCK_INT_RESTORE(listener->accept_lock, rflags);

    auto const inode = (vfs_inode_t*)kzalloc(sizeof(vfs_inode_t));
    if (!inode)
    {
        socket_unregister(child);
        socket_put(listener);
        return -ENOMEM;
    }
    inode->flags = VFS_PIPE;
    inode->ref = 1;
    inode->iops = &socket_iops;
    inode->device = child;

    file_descriptor_t *new_desc = fd_alloc(inode, O_RDWR);
    if (!new_desc)
    {
        kfree(inode);
        socket_unregister(child);
        socket_put(listener);
        return -ENOMEM;
    }
    int new_fd = fd_assign(new_desc, 3);
    if (new_fd == -1)
    {
        kfree(new_desc);
        kfree(inode);
        socket_unregister(child);
        socket_put(listener);
        return -EBUFFULL;
    }

    if (addr)
    {
        struct sockaddr_in out = {0};
        out.sin_family = AF_INET;
        out.sin_port = child->remote.port;
        memcpy(out.sin_addr, child->remote.ip, sizeof(out.sin_addr));
        size_t copy_len = caller_len < actual_len ? caller_len : actual_len;
        if (copy_len > 0 && !copy_to_user(addr, &out, copy_len))
        {
            sys_close(new_fd);
            socket_put(listener);
            return -EFAULT;
        }
        socklen_t out_len = actual_len;
        if (!copy_to_user(addrlen, &out_len, sizeof(out_len)))
        {
            sys_close(new_fd);
            socket_put(listener);
            return -EFAULT;
        }
    }

    socket_put(listener);
    return new_fd;
}
