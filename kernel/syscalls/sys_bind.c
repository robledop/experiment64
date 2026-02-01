#include <syscall_common.h>

#include <lib/string.h>
#include <net/socket.h>

int sys_bind(const int fd, const struct sockaddr *addr, const size_t addrlen)
{
    if (fd < 0 || fd >= MAX_FDS)
        return -1;
    if (!addr)
        return -1;
    if (addrlen < sizeof(struct sockaddr_in))
        return -1;

    file_descriptor_t *desc = fd_get(fd);
    if (!desc)
        return -1;
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

    auto const sock = (socket_t *)desc->inode->device;
    if (!sock)
    {
        fd_put(desc);
        return -1;
    }
    socket_hold(sock);
    fd_put(desc);
    if (sock->state != SOCKET_STATE_UNBOUND)
    {
        socket_put(sock);
        return -1;
    }

    struct sockaddr_in in = {0};
    if (!copy_from_user(&in, addr, sizeof(in)))
    {
        socket_put(sock);
        return -1;
    }
    if (in.sin_family != AF_INET)
    {
        socket_put(sock);
        return -1;
    }

    uint16_t port = 0;
    if (socket_assign_port(sock, in.sin_addr, in.sin_port, &port) != 0)
    {
        socket_put(sock);
        return -1;
    }

    memcpy(sock->local.ip, in.sin_addr, sizeof(sock->local.ip));
    sock->local.port = port;
    sock->state      = SOCKET_STATE_BOUND;
    socket_put(sock);
    return 0;
}
