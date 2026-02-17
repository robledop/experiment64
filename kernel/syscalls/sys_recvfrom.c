#include <syscall_common.h>
#include <lib/string.h>
#include <lib/util.h>
#include <mem/heap.h>
#include <net/socket.h>
#include <status.h>

int sys_recvfrom(const int fd, void *buf, const size_t len, const int flags,
                 struct sockaddr *src_addr, socklen_t *addrlen)
{
    if (fd < 0 || fd >= MAX_FDS)
        return -EBADF;
    if (len == 0)
        return 0;
    if (!buf)
        return -EINVAL;
    if (!user_ptr_write_ok(buf, len, "sys_recvfrom"))
        return -EFAULT;
    if (src_addr && !user_ptr_write_ok(src_addr, sizeof(struct sockaddr_in), "sys_recvfrom"))
        return -EFAULT;
    if (addrlen && !user_ptr_write_ok(addrlen, sizeof(socklen_t), "sys_recvfrom"))
        return -EFAULT;

    file_descriptor_t *desc = fd_get(fd);
    if (!desc)
        return -EBADF;
    if (!desc->inode) {
        fd_put(desc);
        return -EBADF;
    }
    if (desc->inode->iops != &socket_iops) {
        fd_put(desc);
        return -ENOTSUP;
    }

    auto sock = (socket_t *)desc->inode->device;
    if (!sock) {
        fd_put(desc);
        return -EIO;
    }
    socket_hold(sock);
    fd_put(desc);

    const bool block        = (flags & MSG_DONTWAIT) == 0;
    socket_rx_packet_t *pkt = socket_rx_pop(sock, block);
    if (!pkt) {
        const int res = socket_rx_is_closed(sock) ? 0 : -EAGAIN;
        socket_put(sock);
        return res;
    }

    size_t copy_len = (pkt->len < len) ? pkt->len : len;
    if (copy_len > 0)
        memcpy(buf, pkt->data, copy_len);

    if (src_addr) {
        struct sockaddr_in out = {0};
        out.sin_family         = AF_INET;
        out.sin_port           = pkt->from.port;
        memcpy(out.sin_addr, pkt->from.ip, sizeof(out.sin_addr));
        if (!copy_to_user(src_addr, &out, sizeof(out))) {
            if (pkt->data)
                kfree(pkt->data);
            kfree(pkt);
            socket_put(sock);
            return -EFAULT;
        }
    }

    if (addrlen) {
        socklen_t out_len = sizeof(struct sockaddr_in);
        if (!copy_to_user(addrlen, &out_len, sizeof(out_len))) {
            if (pkt->data)
                kfree(pkt->data);
            kfree(pkt);
            socket_put(sock);
            return -EFAULT;
        }
    }

    if (pkt->data)
        kfree(pkt->data);
    kfree(pkt);
    socket_put(sock);
    return clamp_to_int(copy_len);
}
