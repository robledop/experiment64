#include <lib/string.h>
#include <mem/heap.h>
#include <net/socket.h>
#include <net/tcp.h>
#include <sys/fcntl.h>
#include <task/process.h>
#include <net/helpers.h>

static uint64_t socket_inode_read(const vfs_inode_t* node, uint64_t offset, uint64_t size, uint8_t* buffer)
{
    (void)offset;
    if (!node || !buffer) return 0;
    if (size == 0) return 0;

    auto sock = (socket_t*)node->device;
    if (!sock) return 0;

    socket_rx_packet_t* pkt = socket_rx_pop(sock, true);
    if (!pkt) return 0;

    const size_t copy_len = (pkt->len < size) ? pkt->len : size;
    if (copy_len > 0)
        memcpy(buffer, pkt->data, copy_len);

    if (pkt->data) kfree(pkt->data);
    kfree(pkt);
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
        socket_unregister(sock);
        node->device = nullptr;
        kfree(sock);
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
    if (domain != PF_INET) return -1;
    if (protocol == 0)
        protocol = (type == SOCK_DGRAM) ? IPPROTO_UDP : IPPROTO_ICMP;

    if ((type == SOCK_DGRAM && protocol != IPPROTO_UDP) ||
        (type == SOCK_RAW && protocol != IPPROTO_ICMP))
    {
        return -1;
    }

    int fd = -1;
    for (int i = 3; i < MAX_FDS; i++)
    {
        if (current_process->fd_table[i] == nullptr)
        {
            fd = i;
            break;
        }
    }
    if (fd == -1) return -1;

    auto const sock = (socket_t*)kzalloc(sizeof(socket_t));
    if (!sock) return -1;
    sock->domain = domain;
    sock->type = type;
    sock->protocol = protocol;
    sock->state = SOCKET_STATE_UNBOUND;
    sock->ref = 1;

    auto const inode = (vfs_inode_t*)kzalloc(sizeof(vfs_inode_t));
    if (!inode)
    {
        kfree(sock);
        return -1;
    }
    inode->flags = VFS_PIPE;
    inode->ref = 1;
    inode->iops = &socket_iops;
    inode->device = sock;

    auto const desc = (file_descriptor_t*)kzalloc(sizeof(file_descriptor_t));
    if (!desc)
    {
        kfree(inode);
        kfree(sock);
        return -1;
    }
    desc->inode = inode;
    desc->offset = 0;
    desc->flags = O_RDWR;
    desc->ref = 1;

    current_process->fd_table[fd] = desc;
    socket_register(sock);
    return fd;
}
