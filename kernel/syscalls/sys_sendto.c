#include <arpa/inet.h>
#include <lib/string.h>
#include <lib/util.h>
#include <mem/heap.h>
#include <net/arp.h>
#include <net/ethernet.h>
#include <net/helpers.h>
#include <net/icmp.h>
#include <net/ipv4.h>
#include <net/network.h>
#include <net/socket.h>
#include <net/tcp.h>
#include <net/udp.h>
#include <task/process.h>

static bool ip_is_zero(const uint8_t ip[static 4])
{
    return ip[0] == 0 && ip[1] == 0 && ip[2] == 0 && ip[3] == 0;
}

int tcp_sendto(const void* buf, const size_t len, const struct sockaddr* dest_addr, socket_t* const sock,
               struct sockaddr_in in)
{
    if (sock->state != SOCKET_STATE_CONNECTED) return -1;
    if ((sock->flags & SOCKET_FLAG_TCP_ESTABLISHED) == 0) return -1;
    if (ip_is_zero(sock->remote.ip) || sock->remote.port == 0) return -1;
    if (dest_addr &&
        (memcmp(sock->remote.ip, in.sin_addr, sizeof(sock->remote.ip)) != 0 ||
            sock->remote.port != in.sin_port))
    {
        return -1;
    }
    if (len == 0) return 0;

    constexpr size_t max_payload = ETH_DATA_LEN - sizeof(struct ipv4_header) - sizeof(struct tcp_header);
    auto data = (const uint8_t*)buf;
    size_t sent_total = 0;

    while (sent_total < len)
    {
        size_t chunk = len - sent_total;
        if (chunk > max_payload)
            chunk = max_payload;

        uint8_t flags = TCP_FLAG_ACK;
        if (sent_total + chunk == len)
            flags |= TCP_FLAG_PSH;

        if (tcp_send_segment(sock, sock->remote.ip, sock->remote.port,
                             sock->tcp_send_next, sock->tcp_recv_next,
                             flags, data + sent_total, chunk, nullptr) != 0)
            return (sent_total > 0) ? clamp_to_int(sent_total) : -1;

        sock->tcp_send_next += (uint32_t)chunk;
        sent_total += chunk;
    }

    return clamp_to_int(sent_total);
}

int udp_sendto(const void* buf, const size_t len, socket_t* const sock, struct sockaddr_in in, const uint8_t* my_ip,
               uint8_t src_ip[static 4])
{
    if (in.sin_port == 0) return -1;
    if (sock->state == SOCKET_STATE_UNBOUND)
    {
        uint16_t port = 0;
        if (socket_assign_port(sock, my_ip, 0, &port) != 0)
            return -1;
        memcpy(sock->local.ip, my_ip, sizeof(sock->local.ip));
        sock->local.port = port;
        sock->state = SOCKET_STATE_BOUND;
        memcpy(src_ip, my_ip, sizeof(uint8_t) * 4);
    }

    uint8_t next_hop[4];
    select_next_hop(in.sin_addr, next_hop);
    const struct arp_cache_entry entry = arp_cache_find(next_hop);
    if (entry.ip[0] == 0)
    {
        arp_send_request(next_hop);
        return -1;
    }

    const uint8_t* src_mac = network_get_my_mac_address();
    if (!src_mac) return -1;

    const size_t total_len = sizeof(struct ether_header) + sizeof(struct ipv4_header) +
        sizeof(struct udp_header) + len;
    uint8_t* packet = kmalloc(total_len);
    if (!packet) return -1;

    auto const eth = (struct ether_header*)packet;
    memcpy(eth->dest_host, entry.mac, 6);
    memcpy(eth->src_host, src_mac, 6);
    eth->ether_type = htons(ETHERTYPE_IP);

    auto const ip = (struct ipv4_header*)(packet + sizeof(struct ether_header));
    ip->version = 4;
    ip->ihl = 0x05;
    ip->dscp_ecn = 0;
    ip->total_length = htons(sizeof(struct ipv4_header) + sizeof(struct udp_header) + len);
    ip->identification = 0;
    ip->flags_fragment_offset = 0;
    ip->ttl = 64;
    ip->protocol = IP_PROTOCOL_UDP;
    ip->header_checksum = 0;
    memcpy(ip->source_ip, src_ip, 4);
    memcpy(ip->dest_ip, in.sin_addr, 4);
    ip->header_checksum = checksum(ip, (int)sizeof(struct ipv4_header), 0);

    auto const udp = (struct udp_header*)((uint8_t*)ip + sizeof(struct ipv4_header));
    udp->src_port = sock->local.port;
    udp->dest_port = in.sin_port;
    udp->len = htons(sizeof(struct udp_header) + len);
    udp->checksum = 0;

    uint8_t* payload = (uint8_t*)udp + sizeof(struct udp_header);
    if (len > 0)
        memcpy(payload, buf, len);

    const struct udp_pseudo_header pseudo = {
        .src_ip = {src_ip[0], src_ip[1], src_ip[2], src_ip[3]},
        .dest_ip = {in.sin_addr[0], in.sin_addr[1], in.sin_addr[2], in.sin_addr[3]},
        .zero = 0,
        .protocol = IP_PROTOCOL_UDP,
        .udp_length = udp->len,
    };

    const size_t checksum_len = sizeof(struct udp_pseudo_header) + sizeof(struct udp_header) + len;
    uint8_t* checksum_buf = kmalloc(checksum_len);
    if (!checksum_buf)
    {
        kfree(packet);
        return -1;
    }
    memcpy(checksum_buf, &pseudo, sizeof(struct udp_pseudo_header));
    memcpy(checksum_buf + sizeof(struct udp_pseudo_header), udp, sizeof(struct udp_header));
    if (len > 0)
        memcpy(checksum_buf + sizeof(struct udp_pseudo_header) + sizeof(struct udp_header), payload, len);
    udp->checksum = checksum(checksum_buf, (int)checksum_len, 0);
    kfree(checksum_buf);

    network_send_packet(packet, (uint16_t)total_len);
    kfree(packet);
    return clamp_to_int(len);
}

int icmp_sendto(const void* buf, const size_t len, struct sockaddr_in in, uint8_t src_ip[4])
{
    if (len < sizeof(struct icmp_header)) return -1;

    uint8_t next_hop[4];
    select_next_hop(in.sin_addr, next_hop);
    const struct arp_cache_entry entry = arp_cache_find(next_hop);
    if (entry.ip[0] == 0)
    {
        arp_send_request(next_hop);
        return -1;
    }

    const uint8_t* src_mac = network_get_my_mac_address();
    if (!src_mac)
        return -1;

    const size_t total_len = sizeof(struct ether_header) + sizeof(struct ipv4_header) + len;
    uint8_t* packet = kmalloc(total_len);
    if (!packet)
        return -1;

    auto const eth = (struct ether_header*)packet;
    memcpy(eth->dest_host, entry.mac, 6);
    memcpy(eth->src_host, src_mac, 6);
    eth->ether_type = htons(ETHERTYPE_IP);

    auto const ip = (struct ipv4_header*)(packet + sizeof(struct ether_header));
    ip->version = 4;
    ip->ihl = 0x05;
    ip->dscp_ecn = 0;
    ip->total_length = htons(sizeof(struct ipv4_header) + len);
    ip->identification = 0;
    ip->flags_fragment_offset = 0;
    ip->ttl = 64;
    ip->protocol = IP_PROTOCOL_ICMP;
    ip->header_checksum = 0;
    memcpy(ip->source_ip, src_ip, 4);
    memcpy(ip->dest_ip, in.sin_addr, 4);
    ip->header_checksum = checksum(ip, (int)sizeof(struct ipv4_header), 0);

    uint8_t* icmp_data = (uint8_t*)ip + sizeof(struct ipv4_header);
    if (len > 0)
        memcpy(icmp_data, buf, len);
    auto const icmp = (struct icmp_header*)icmp_data;
    icmp->checksum = 0;
    icmp->checksum = checksum(icmp_data, (int)len, 0);

    network_send_packet(packet, (uint16_t)total_len);
    kfree(packet);
    return clamp_to_int(len);
}

int sys_sendto(const int fd, const void* buf, const size_t len, const int flags,
               const struct sockaddr* dest_addr, const socklen_t addrlen)
{
    (void)flags;
    if (fd < 0 || fd >= MAX_FDS) return -1;
    if (!buf && len > 0) return -1;

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
    if (dest_addr)
    {
        if (addrlen < sizeof(struct sockaddr_in)) return -1;
        memcpy(&in, dest_addr, sizeof(in));
        if (in.sin_family != AF_INET) return -1;
    }
    else if (sock->protocol != IPPROTO_TCP || sock->type != SOCK_STREAM)
    {
        return -1;
    }

    if (sock->protocol == IPPROTO_TCP && sock->type == SOCK_STREAM)
        return tcp_sendto(buf, len, dest_addr, sock, in);

    if (sock->protocol == IPPROTO_UDP && sock->type == SOCK_DGRAM)
        return udp_sendto(buf, len, sock, in, my_ip, src_ip);

    if (sock->protocol == IPPROTO_ICMP && sock->type == SOCK_RAW)
        return icmp_sendto(buf, len, in, src_ip);

    return -1;
}
