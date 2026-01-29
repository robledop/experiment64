#include <lib/string.h>
#include <net/socket.h>
#include <task/process.h>

int sys_bind(const int fd, const struct sockaddr *addr, const size_t addrlen)
{
    if (fd < 0 || fd >= MAX_FDS)
        return -1;
    if (!addr)
        return -1;
    if (addrlen < sizeof(struct sockaddr_in))
        return -1;

    file_descriptor_t *desc = current_process->fd_table[fd];
    if (!desc || !desc->inode)
        return -1;
    if (desc->inode->iops != &socket_iops)
        return -1;

    auto const sock = (socket_t *)desc->inode->device;
    if (!sock)
        return -1;
    if (sock->state != SOCKET_STATE_UNBOUND)
        return -1;

    struct sockaddr_in in = {0};
    memcpy(&in, addr, sizeof(in));
    if (in.sin_family != AF_INET)
        return -1;

    uint16_t port = 0;
    if (socket_assign_port(sock, in.sin_addr, in.sin_port, &port) != 0)
        return -1;

    memcpy(sock->local.ip, in.sin_addr, sizeof(sock->local.ip));
    sock->local.port = port;
    sock->state      = SOCKET_STATE_BOUND;
    return 0;
}