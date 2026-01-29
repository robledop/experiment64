#include <syscall_common.h>

#include <lib/string.h>
#include <net/helpers.h>
#include <net/icmp.h>
#include <net/network.h>
#include <net/socket.h>
#include <net/tcp.h>
#include <net/udp.h>
#include <task/process.h>

int sys_sendto(const int fd, const void* buf, const size_t len, const int flags,
               const struct sockaddr* dest_addr, const socklen_t addrlen)
{
    (void)flags;
    if (fd < 0 || fd >= MAX_FDS) return -1;
    if (!buf && len > 0) return -1;
    if (len > 0 && !user_ptr_read_ok(buf, len, "sys_sendto")) return -1;

    file_descriptor_t* desc = current_process->fd_table[fd];
    if (!desc || !desc->inode) return -1;
    if (desc->inode->iops != &socket_iops) return -1;

    auto const sock = (socket_t*)desc->inode->device;
    if (!sock) return -1;

    const uint8_t* my_ip = network_get_my_ip_address();
    if (!my_ip) return -1;
    uint8_t src_ip[4];
    // If the IP is 0.0.0.0, that is local, so use our own IP
    if (ip_is_zero(sock->local.ip))
        memcpy(src_ip, my_ip, sizeof(src_ip));
    else
        memcpy(src_ip, sock->local.ip, sizeof(src_ip));

    struct sockaddr_in in = {0};
    const struct sockaddr* dest_check = nullptr;
    if (dest_addr)
    {
        if (addrlen < sizeof(struct sockaddr_in)) return -1;
        if (!copy_from_user(&in, dest_addr, sizeof(in))) return -1;
        if (in.sin_family != AF_INET) return -1;
        dest_check = (const struct sockaddr*)&in;
    }
    else if (sock->protocol != IPPROTO_TCP || sock->type != SOCK_STREAM)
    {
        return -1;
    }

    if (sock->protocol == IPPROTO_TCP && sock->type == SOCK_STREAM)
        return tcp_sendto(buf, len, dest_check, sock, in);

    if (sock->protocol == IPPROTO_UDP && sock->type == SOCK_DGRAM)
        return udp_sendto(buf, len, sock, in, my_ip, src_ip);

    if (sock->protocol == IPPROTO_ICMP && sock->type == SOCK_RAW)
        return icmp_sendto(buf, len, in, src_ip);

    return -1;
}
