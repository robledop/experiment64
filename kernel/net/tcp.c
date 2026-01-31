#include <net/tcp.h>
#include <arpa/inet.h>
#include <net/arp.h>
#include <net/ethernet.h>
#include <net/helpers.h>
#include <net/ipv4.h>
#include <net/network.h>
#include <net/socket.h>
#include <task/process.h>
#include <mem/heap.h>
#include <lib/string.h>
#include <attributes.h>
#include <lib/util.h>

struct tcp_pseudo_header
{
    uint8_t src_ip[4];
    uint8_t dest_ip[4];
    uint8_t zero;
    uint8_t protocol;
    uint16_t tcp_length;
} __attribute__((packed));

// Next Sequence Number
static uint32_t tcp_next_isn = 1;

static uint32_t tcp_generate_isn(void)
{
    return tcp_next_isn++;
}

int tcp_send_segment(const socket_t* sock, const uint8_t dest_ip[static 4], const uint16_t dest_port,
                     const uint32_t seq_num, const uint32_t ack_num, const uint8_t flags,
                     const uint8_t* payload, const size_t payload_len, const uint8_t* dest_mac)
{
    const uint8_t* my_ip = network_get_my_ip_address();
    const uint8_t* my_mac = network_get_my_mac_address();
    if (!my_ip || !my_mac)
        return -1;

    uint8_t src_ip[4];
    if (ip_is_zero(sock->local.ip))
        memcpy(src_ip, my_ip, sizeof(src_ip));
    else
        memcpy(src_ip, sock->local.ip, sizeof(src_ip));

    const uint8_t* out_mac = dest_mac;
    uint8_t resolved_mac[6];
    if (!out_mac)
    {
        uint8_t next_hop[4];
        network_select_next_hop(dest_ip, next_hop);
        const struct arp_cache_entry entry = arp_resolve(next_hop);
        if (entry.ip[0] == 0) return -1;

        memcpy(resolved_mac, entry.mac, sizeof(resolved_mac));
        out_mac = resolved_mac;
    }

    constexpr size_t eth_len = sizeof(struct ether_header);
    constexpr size_t ip_header_len = sizeof(struct ipv4_header);
    constexpr size_t tcp_header_len = sizeof(struct tcp_header);
    const size_t total_len = eth_len + ip_header_len + tcp_header_len + payload_len;

    uint8_t* packet = kmalloc(total_len);
    if (!packet)
        return -1;

    auto const eth = (struct ether_header*)packet;
    memcpy(eth->dest_host, out_mac, 6);
    memcpy(eth->src_host, my_mac, 6);
    eth->ether_type = htons(ETHERTYPE_IP);

    auto const ip = (struct ipv4_header*)(packet + eth_len);
    ip->version = 4;
    ip->ihl = (uint8_t)(ip_header_len / 4);
    ip->dscp_ecn = 0;
    ip->total_length = htons((uint16_t)(ip_header_len + tcp_header_len + payload_len));
    ip->identification = 0;
    ip->flags_fragment_offset = 0;
    ip->ttl = 64;
    ip->protocol = IP_PROTOCOL_TCP;
    ip->header_checksum = 0;
    memcpy(ip->source_ip, src_ip, sizeof(src_ip));
    memcpy(ip->dest_ip, dest_ip, 4);
    ip->header_checksum = checksum(ip, (int)ip_header_len, 0);

    auto const tcp = (struct tcp_header*)((uint8_t*)ip + ip_header_len);
    memset(tcp, 0, sizeof(*tcp));
    tcp->src_port = sock->local.port;
    tcp->dst_port = dest_port;
    tcp->seq_num = htonl(seq_num);
    tcp->ack_num = htonl(ack_num);
    tcp->data_offset_reserved = (uint8_t)((tcp_header_len / 4) << 4);
    tcp->flags = flags;
    tcp->window = htons(4096);
    tcp->checksum = 0;
    tcp->urgent_ptr = 0;

    if (payload_len > 0)
        memcpy((uint8_t*)tcp + tcp_header_len, payload, payload_len);

    const struct tcp_pseudo_header pseudo = {
        .src_ip = {src_ip[0], src_ip[1], src_ip[2], src_ip[3]},
        .dest_ip = {dest_ip[0], dest_ip[1], dest_ip[2], dest_ip[3]},
        .zero = 0,
        .protocol = IP_PROTOCOL_TCP,
        .tcp_length = htons((uint16_t)(tcp_header_len + payload_len)),
    };

    const size_t checksum_len = sizeof(struct tcp_pseudo_header) + tcp_header_len + payload_len;
    uint8_t* checksum_buf = kmalloc(checksum_len);
    if (!checksum_buf)
    {
        kfree(packet);
        return -1;
    }
    memcpy(checksum_buf, &pseudo, sizeof(struct tcp_pseudo_header));
    memcpy(checksum_buf + sizeof(struct tcp_pseudo_header), tcp, tcp_header_len);
    if (payload_len > 0)
        memcpy(checksum_buf + sizeof(struct tcp_pseudo_header) + tcp_header_len, payload, payload_len);
    tcp->checksum = checksum(checksum_buf, (int)checksum_len, 0);
    kfree(checksum_buf);

    int res = network_send_packet(packet, (uint16_t)total_len);
    kfree(packet);
    return res;
}

void NONNULL tcp_receive(uint8_t* packet, const uint16_t len, const size_t ip_len, const size_t ip_header_len)
{
    constexpr size_t eth_len = sizeof(struct ether_header);
    if (len < eth_len + ip_header_len) return;
    if (ip_len < ip_header_len + sizeof(struct tcp_header)) return;
    if (len < eth_len + ip_len) return;

    const struct ether_header* ether_header = (struct ether_header*)packet;
    auto ipv4_header = (struct ipv4_header*)(packet + eth_len);

    const size_t tcp_offset = eth_len + ip_header_len;
    auto tcp_header = (struct tcp_header*)(packet + tcp_offset);

    const size_t tcp_header_len = ((tcp_header->data_offset_reserved >> 4) & 0x0F) * 4;
    if (tcp_header_len < sizeof(struct tcp_header)) return;
    if (ip_len < ip_header_len + tcp_header_len) return;
    if (len < tcp_offset + tcp_header_len) return;

    const size_t tcp_len = ip_len - ip_header_len;
    const size_t payload_len = tcp_len - tcp_header_len;
    const uint8_t* payload = packet + tcp_offset + tcp_header_len;

    struct arp_cache_entry cache_entry = arp_cache_find(ipv4_header->source_ip);
    if (cache_entry.ip[0] == 0)
        arp_cache_add(ipv4_header->source_ip, (uint8_t*)ether_header->src_host);

    const uint8_t flags = tcp_header->flags;
    const uint32_t seq_num = ntohl(tcp_header->seq_num);
    const uint32_t ack_num = ntohl(tcp_header->ack_num);

    socket_t* sock = socket_find_tcp_connected(ipv4_header->dest_ip, tcp_header->dst_port,
                                               ipv4_header->source_ip, tcp_header->src_port);

    if (!sock && (flags & TCP_FLAG_SYN) && !(flags & TCP_FLAG_ACK))
    {
        socket_t* listener = socket_find_tcp_listener(ipv4_header->dest_ip, tcp_header->dst_port);
        if (!listener)
            return;

        const int backlog = (listener->backlog > 0) ? listener->backlog : 1;
        uint64_t rflags;
        SPIN_LOCK_INT_SAVE(listener->accept_lock, rflags);
        const bool backlog_full = listener->accept_queue_len >= (size_t)backlog;
        SPIN_UNLOCK_INT_RESTORE(listener->accept_lock, rflags);
        if (backlog_full)
            return;

        auto child = (socket_t*)kzalloc(sizeof(socket_t));
        if (!child)
            return;
        child->domain = listener->domain;
        child->type = listener->type;
        child->protocol = listener->protocol;
        child->state = SOCKET_STATE_CONNECTED;
        memcpy(child->local.ip, listener->local.ip, sizeof(child->local.ip));
        child->local.port = listener->local.port;
        memcpy(child->remote.ip, ipv4_header->source_ip, sizeof(child->remote.ip));
        child->remote.port = tcp_header->src_port;
        child->flags = SOCKET_FLAG_TCP_SYN_RCVD;
        child->tcp_recv_next = seq_num + 1;
        const uint32_t isn = tcp_generate_isn();
        child->tcp_send_next = isn + 1;
        socket_register(child);

        tcp_send_segment(child, ipv4_header->source_ip, tcp_header->src_port,
                         isn, child->tcp_recv_next, (uint8_t)(TCP_FLAG_SYN | TCP_FLAG_ACK),
                         nullptr, 0, ether_header->src_host);
        return;
    }

    if (!sock) return;

    if ((sock->flags & SOCKET_FLAG_TCP_SYN_RCVD) != 0)
    {
        if ((flags & TCP_FLAG_ACK) && ack_num == sock->tcp_send_next)
        {
            sock->flags &= ~SOCKET_FLAG_TCP_SYN_RCVD;
            sock->flags |= SOCKET_FLAG_TCP_ESTABLISHED;
            socket_t* listener = socket_find_tcp_listener(ipv4_header->dest_ip, tcp_header->dst_port);
            if (listener)
            {
                const int backlog = (listener->backlog > 0) ? listener->backlog : 1;
                bool queued = false;
                uint64_t rflags;
                SPIN_LOCK_INT_SAVE(listener->accept_lock, rflags);
                if (listener->accept_queue_len < (size_t)backlog)
                {
                    list_add_tail(&sock->accept_list, &listener->accept_queue);
                    listener->accept_queue_len++;
                    queued = true;
                }
                SPIN_UNLOCK_INT_RESTORE(listener->accept_lock, rflags);
                if (queued)
                    thread_wakeup(listener);
                else
                {
                    socket_unregister(sock);
                    kfree(sock);
                    return;
                }
            }
        }
        else
        {
            return;
        }
    }

    if ((sock->flags & SOCKET_FLAG_TCP_ESTABLISHED) == 0)
        return;

    bool should_ack = false;
    if (payload_len > 0)
    {
        if (seq_num == sock->tcp_recv_next)
        {
            socket_deliver_tcp(ipv4_header->dest_ip, tcp_header->dst_port,
                               ipv4_header->source_ip, tcp_header->src_port,
                               payload, payload_len);
            sock->tcp_recv_next += (uint32_t)payload_len;
            should_ack = true;
        }
        else
        {
            should_ack = true;
        }
    }

    if (flags & TCP_FLAG_FIN)
    {
        sock->tcp_recv_next += 1;
        should_ack = true;
    }

    if (should_ack)
    {
        tcp_send_segment(sock, ipv4_header->source_ip, tcp_header->src_port,
                         sock->tcp_send_next, sock->tcp_recv_next,
                         TCP_FLAG_ACK, nullptr, 0, ether_header->src_host);
    }
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
