#include <syscall_common.h>

#include <mem/heap.h>
#include <net/socket.h>
#include <net/tcp.h>
#include <sys/fcntl.h>
#include <net/helpers.h>
#include <status.h>

static uint64_t socket_inode_read(const vfs_inode_t* node, uint64_t offset, uint64_t size, uint8_t* buffer)
{
    (void)offset;
    if (!node || !buffer) return 0;
    if (size == 0) return 0;

    auto sock = (socket_t*)node->device;
    if (!sock) return 0;
    socket_hold(sock);

    socket_rx_packet_t* pkt = socket_rx_pop(sock, true);
    if (!pkt)
    {
        socket_put(sock);
        return 0;
    }

    const size_t copy_len = (pkt->len < size) ? pkt->len : size;
    if (copy_len > 0)
        memcpy(buffer, pkt->data, copy_len);

    if (pkt->data) kfree(pkt->data);
    kfree(pkt);
    socket_put(sock);
    return copy_len;
}

static void socket_send_tcp_fin(socket_t* sock)
{
    if (!sock)
        return;
    if (sock->protocol != IPPROTO_TCP || sock->type != SOCK_STREAM)
        return;
    if ((sock->flags & SOCKET_FLAG_TCP_ESTABLISHED) == 0)
        return;
    if (sock->flags & SOCKET_FLAG_TCP_FIN_SENT)
        return;
    if (ip_is_zero(sock->remote.ip) || sock->remote.port == 0)
        return;

    tcp_send_segment(sock, sock->remote.ip, sock->remote.port,
                     sock->tcp_send_next, sock->tcp_recv_next,
                     (uint8_t)(TCP_FLAG_FIN | TCP_FLAG_ACK), nullptr, 0, nullptr);
    sock->tcp_send_next += 1;
    sock->flags |= SOCKET_FLAG_TCP_FIN_SENT;
}

static void socket_inode_close(vfs_inode_t* node)
{
    if (!node) return;
    auto sock = (socket_t*)node->device;
    if (sock)
    {
        if (sock->protocol == IPPROTO_TCP) socket_send_tcp_fin(sock);
        node->device = nullptr;
        socket_unregister(sock);
    }
}

struct inode_operations socket_iops = {
    .read = socket_inode_read,
    .write = nullptr,
    .truncate = nullptr,
    .open = nullptr,
    .close = socket_inode_close,
    .ioctl = nullptr,
    .readdir = nullptr,
    .finddir = nullptr,
    .clone = nullptr,
    .mknod = nullptr,
    .link = nullptr,
    .unlink = nullptr,
    .stat = nullptr,
};

int sys_socket(const int domain, const int type, int protocol)
{
    if (domain != PF_INET)
        return -ENOTSUP;
    if (type != SOCK_STREAM && type != SOCK_DGRAM && type != SOCK_RAW)
        return -EINVAL;
    if (protocol == 0) {
        if (type == SOCK_STREAM)
            protocol = IPPROTO_TCP;
        else if (type == SOCK_DGRAM)
            protocol = IPPROTO_UDP;
        else
            protocol = IPPROTO_ICMP;
    }

    const bool valid_combo = (type == SOCK_STREAM && protocol == IPPROTO_TCP) ||
        (type == SOCK_DGRAM && protocol == IPPROTO_UDP) ||
        (type == SOCK_RAW && protocol == IPPROTO_ICMP);
    if (!valid_combo)
        return -EINVAL;

    auto const sock = (socket_t*)kzalloc(sizeof(socket_t));
    if (!sock)
        return -ENOMEM;
    sock->domain = domain;
    sock->type = type;
    sock->protocol = protocol;
    sock->state = SOCKET_STATE_UNBOUND;
    sock->flags |= SOCKET_FLAG_HEAP_ALLOC;
    sock->ref = 1;

    auto const inode = (vfs_inode_t*)kzalloc(sizeof(vfs_inode_t));
    if (!inode)
    {
        socket_put(sock);
        return -ENOMEM;
    }
    inode->flags = VFS_PIPE;
    inode->ref = 1;
    inode->iops = &socket_iops;
    inode->device = sock;

    file_descriptor_t *desc = fd_alloc(inode, O_RDWR);
    if (!desc)
    {
        kfree(inode);
        socket_put(sock);
        return -ENOMEM;
    }
    int fd = fd_assign(desc, 3);
    if (fd == -1)
    {
        kfree(desc);
        kfree(inode);
        socket_put(sock);
        return -EBUFFULL;
    }
    socket_register(sock);
    return fd;
}
