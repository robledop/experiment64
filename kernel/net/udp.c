#include <net/udp.h>
#include <arpa/inet.h>
#include <net/dhcp.h>
#include <net/socket.h>
#include <mem/heap.h>
#include <net/arp.h>
#include <net/network.h>
#include <lib/string.h>
#include <lib/util.h>
#include <net/helpers.h>

void udp_receive(uint8_t* packet, const uint16_t len, const size_t ip_len, const size_t ip_header_len)
{
    constexpr size_t eth_len = sizeof(struct ether_header);
    auto ipv4_header = (struct ipv4_header*)(packet + eth_len);

    const size_t udp_off = eth_len + ip_header_len;
    if (len < udp_off + sizeof(struct udp_header)) return;

    auto udp_header = (struct udp_header*)(packet + udp_off);
    const size_t udp_len = ntohs(udp_header->len);
    if (udp_len < sizeof(struct udp_header) || udp_len > ip_len - ip_header_len) return;

    if (udp_header->dest_port == htons(DHCP_SOURCE_PORT))
        dhcp_receive(packet);

    const size_t payload_len = udp_len - sizeof(struct udp_header);
    const uint8_t* payload = packet + udp_off + sizeof(struct udp_header);
    socket_deliver_udp(ipv4_header->dest_ip, udp_header->dest_port,
                       ipv4_header->source_ip, udp_header->src_port,
                       payload, payload_len);
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
    network_select_next_hop(in.sin_addr, next_hop);
    const struct arp_cache_entry entry = arp_resolve(next_hop);
    if (entry.ip[0] == 0) return -1;

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
