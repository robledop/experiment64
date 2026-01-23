#include <net/helpers.h>
#include <net/icmp.h>
#include <net/network.h>
#include <mem/heap.h>
#include <lib/string.h>
#include <arpa/inet.h>
#include <net/socket.h>
#include <drivers/terminal.h>
#include <lib/util.h>
#include <net/arp.h>

void icmp_send_echo_reply(const uint8_t* packet, const uint16_t len,
                          const size_t ip_len, const size_t ip_header_len)
{
    constexpr size_t eth_len = sizeof(struct ether_header);
    if (len < eth_len + ip_header_len) return;
    if (ip_len < ip_header_len + sizeof(struct icmp_header)) return;
    if (len < eth_len + ip_len) return;

    const uint8_t* my_mac = network_get_my_mac_address();
    const uint8_t* my_ip = network_get_my_ip_address();
    if (!my_mac || !my_ip) return;

    const struct ether_header* ether_header = (struct ether_header*)packet;
    const struct ipv4_header* ipv4_header = (struct ipv4_header*)(packet + eth_len);
    const uint8_t* icmp_data = packet + eth_len + ip_header_len;
    const size_t icmp_len = ip_len - ip_header_len;
    constexpr size_t icmp_header_len = sizeof(struct icmp_header);
    const size_t payload_len = icmp_len - icmp_header_len;
    const uint8_t* payload = icmp_data + icmp_header_len;

    const size_t reply_len = eth_len + ip_len;
    uint8_t* reply_packet = kmalloc(reply_len);
    if (!reply_packet) return;

    auto const reply_ether_header = (struct ether_header*)reply_packet;
    memcpy(reply_ether_header->dest_host, ether_header->src_host, 6);
    memcpy(reply_ether_header->src_host, my_mac, 6);
    reply_ether_header->ether_type = htons(ETHERTYPE_IP);

    uint8_t* reply_ip_base = reply_packet + eth_len;
    memcpy(reply_ip_base, ipv4_header, ip_header_len);
    auto const reply_ipv4_header = (struct ipv4_header*)reply_ip_base;
    reply_ipv4_header->version = 4;
    reply_ipv4_header->ihl = (uint8_t)(ip_header_len / 4);
    reply_ipv4_header->dscp_ecn = 0;
    reply_ipv4_header->total_length = htons((uint16_t)ip_len);
    reply_ipv4_header->identification = 0;
    reply_ipv4_header->flags_fragment_offset = 0;
    reply_ipv4_header->ttl = 64;
    reply_ipv4_header->protocol = IP_PROTOCOL_ICMP;
    reply_ipv4_header->header_checksum = 0;
    memcpy(reply_ipv4_header->source_ip, my_ip, 4);
    memcpy(reply_ipv4_header->dest_ip, ipv4_header->source_ip, 4);
    reply_ipv4_header->header_checksum = checksum(reply_ipv4_header, (int)ip_header_len, 0);

    uint8_t* reply_icmp_data = reply_packet + eth_len + ip_header_len;
    auto const reply_icmp_header = (struct icmp_header*)reply_icmp_data;
    memcpy(reply_icmp_header, icmp_data, icmp_header_len);
    reply_icmp_header->type = ICMP_REPLY;
    reply_icmp_header->code = 0;
    if (payload_len > 0)
        memcpy(reply_icmp_data + icmp_header_len, payload, payload_len);
    reply_icmp_header->checksum = 0;
    reply_icmp_header->checksum = checksum(reply_icmp_data, (int)icmp_len, 0);

    const int res = network_send_packet(reply_packet, (uint16_t)reply_len);
    if (res != 0)
        boot_message(WARNING, "ICMP reply send failed");
    kfree(reply_packet);
}

void icmp_receive(uint8_t* packet, const uint16_t len, const size_t ip_len, const size_t ip_header_len)
{
    constexpr size_t eth_len = sizeof(struct ether_header);
    if (len < eth_len + ip_header_len) return;
    if (ip_len < ip_header_len + sizeof(struct icmp_header)) return;
    if (len < eth_len + ip_len) return;

    auto const ip_header = (struct ipv4_header*)(packet + eth_len);
    auto icmp_header = (struct icmp_header*)(packet + eth_len + ip_header_len);

    switch (icmp_header->type)
    {
    case ICMP_V4_ECHO:
        {
            icmp_send_echo_reply(packet, len, ip_len, ip_header_len);
            break;
        }
    case ICMP_REPLY:
        {
            const uint8_t* icmp_data = packet + eth_len + ip_header_len;
            const size_t icmp_len = ip_len - ip_header_len;
            socket_deliver_icmp(ip_header->dest_ip, ip_header->source_ip, icmp_data, icmp_len);
            break;
        }
    default:
        break;
    }
}

int icmp_sendto(const void* buf, const size_t len, struct sockaddr_in in, uint8_t src_ip[4])
{
    if (len < sizeof(struct icmp_header)) return -1;

    uint8_t next_hop[4];
    network_select_next_hop(in.sin_addr, next_hop);
    const struct arp_cache_entry entry = arp_resolve(next_hop);
    if (entry.ip[0] == 0) return -1;

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
