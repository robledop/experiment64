#include <syscall_common.h>
#include <lib/string.h>
#include <mem/heap.h>
#include <net/socket.h>
#include <sys/fcntl.h>

int sys_close(int fd);

int sys_accept(const int fd, struct sockaddr* addr, const size_t addrlen)
{
    if (fd < 0 || fd >= MAX_FDS)
        return -1;

    file_descriptor_t* desc = current_process->fd_table[fd];
    if (!desc || !desc->inode)
        return -1;
    if (desc->inode->iops != &socket_iops)
        return -1;

    auto listener = (socket_t*)desc->inode->device;
    if (!listener)
        return -1;
    socket_hold(listener);
    if (listener->type != SOCK_STREAM || listener->protocol != IPPROTO_TCP)
    {
        socket_put(listener);
        return -1;
    }
    if (listener->state != SOCKET_STATE_LISTENING)
    {
        socket_put(listener);
        return -1;
    }
    if (addr && addrlen < sizeof(struct sockaddr_in))
    {
        socket_put(listener);
        return -1;
    }
    if (addr && !user_ptr_write_ok(addr, sizeof(struct sockaddr_in), "sys_accept"))
    {
        socket_put(listener);
        return -1;
    }

    socket_t* child = nullptr;
    uint64_t rflags;
    SPIN_LOCK_INT_SAVE(listener->accept_lock, rflags);
    while (list_empty(&listener->accept_queue))
        thread_sleep(listener, &listener->accept_lock);
    child = list_entry(listener->accept_queue.next, socket_t, accept_list);
    list_del(&child->accept_list);
    if (listener->accept_queue_len > 0)
        listener->accept_queue_len--;
    SPIN_UNLOCK_INT_RESTORE(listener->accept_lock, rflags);

    int new_fd = -1;
    for (int i = 3; i < MAX_FDS; i++)
    {
        if (current_process->fd_table[i] == nullptr)
        {
            new_fd = i;
            break;
        }
    }
    if (new_fd == -1)
    {
        socket_unregister(child);
        socket_put(listener);
        return -1;
    }

    auto const inode = (vfs_inode_t*)kzalloc(sizeof(vfs_inode_t));
    if (!inode)
    {
        socket_unregister(child);
        socket_put(listener);
        return -1;
    }
    inode->flags = VFS_PIPE;
    inode->ref = 1;
    inode->iops = &socket_iops;
    inode->device = child;

    auto const new_desc = (file_descriptor_t*)kzalloc(sizeof(file_descriptor_t));
    if (!new_desc)
    {
        kfree(inode);
        socket_unregister(child);
        socket_put(listener);
        return -1;
    }
    new_desc->inode = inode;
    new_desc->offset = 0;
    new_desc->flags = O_RDWR;
    new_desc->ref = 1;
    current_process->fd_table[new_fd] = new_desc;

    if (addr)
    {
        struct sockaddr_in out = {0};
        out.sin_family = AF_INET;
        out.sin_port = child->remote.port;
        memcpy(out.sin_addr, child->remote.ip, sizeof(out.sin_addr));
        if (!copy_to_user(addr, &out, sizeof(out)))
        {
            sys_close(new_fd);
            socket_put(listener);
            return -1;
        }
    }

    socket_put(listener);
    return new_fd;
}
