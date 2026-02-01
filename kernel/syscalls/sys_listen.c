#include <syscall_common.h>
#include <net/socket.h>

int sys_listen(const int fd, const int backlog)
{
    if (fd < 0 || fd >= MAX_FDS) return -1;
    if (backlog < 0) return -1;

    file_descriptor_t* desc = fd_get(fd);
    if (!desc) return -1;
    if (!desc->inode)
    {
        fd_put(desc);
        return -1;
    }
    if (desc->inode->iops != &socket_iops)
    {
        fd_put(desc);
        return -1;
    }

    auto const sock = (socket_t*)desc->inode->device;
    if (!sock)
    {
        fd_put(desc);
        return -1;
    }
    socket_hold(sock);
    fd_put(desc);
    if (sock->type != SOCK_STREAM || sock->protocol != IPPROTO_TCP)
    {
        socket_put(sock);
        return -1;
    }
    if (sock->state != SOCKET_STATE_BOUND && sock->state != SOCKET_STATE_LISTENING)
    {
        socket_put(sock);
        return -1;
    }

    sock->backlog = (backlog > 0) ? backlog : 1;
    sock->state = SOCKET_STATE_LISTENING;
    socket_put(sock);
    return 0;
}
