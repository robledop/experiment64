#include "net/arp.h"
#include "net/helpers.h"
#include "net/icmp.h"
#include "net/network.h"
#include "heap.h"
#include "string.h"

const char *icmp_request_payload = "osdev icmp request payload";

void icmp_send_echo_reply(uint8_t *packet, const uint16_t len)
{
    const struct ether_header *ether_header = (struct ether_header *)packet;
    const struct ipv4_header *ipv4_header = (struct ipv4_header *)(packet + sizeof(struct ether_header));
    struct icmp_header *icmp_header =
        (struct icmp_header *)(packet + sizeof(struct ether_header) + sizeof(struct ipv4_header));
    void *payload =
        (void *)(packet + sizeof(struct ether_header) + sizeof(struct ipv4_header) + sizeof(struct icmp_header));

    struct icmp_packet *reply_packet = kzalloc(sizeof(struct icmp_packet));
    struct ether_header reply_ether_header;
    memcpy(reply_ether_header.dest_host, ether_header->src_host, 6);
    memcpy(reply_ether_header.src_host, network_get_my_mac_address(), 6);
    reply_ether_header.ether_type = htons(ETHERTYPE_IP);

    reply_packet->ether_header = reply_ether_header;

    struct ipv4_header reply_ipv4_header;
    reply_ipv4_header.version = 4;
    reply_ipv4_header.ihl = 0x05;
    reply_ipv4_header.dscp_ecn = 0;
    reply_ipv4_header.total_length = ntohs(len - sizeof(struct ether_header));
    reply_ipv4_header.flags_fragment_offset = 0x0;
    reply_ipv4_header.ttl = 64;
    reply_ipv4_header.protocol = IP_PROTOCOL_ICMP;
    reply_ipv4_header.header_checksum = 0;
    memcpy(reply_ipv4_header.source_ip, network_get_my_ip_address(), 4);
    memcpy(reply_ipv4_header.dest_ip, ipv4_header->source_ip, 4);

    reply_ipv4_header.header_checksum = checksum((void *)&reply_ipv4_header, reply_ipv4_header.ihl * 4, 0);

    reply_packet->ip_header = reply_ipv4_header;

    icmp_header->type = ICMP_REPLY;
    icmp_header->checksum = 0;
    icmp_header->checksum = checksum(icmp_header, len - sizeof(struct ether_header) - sizeof(struct ipv4_header), 0);

    struct icmp_header reply_icmp_header;
    memcpy(&reply_icmp_header, icmp_header, sizeof(struct icmp_header));

    reply_packet->icmp_header = reply_icmp_header;
    memcpy(reply_packet->payload,
           payload,
           len - sizeof(struct ether_header) - sizeof(struct ipv4_header) - sizeof(struct icmp_header));

    network_send_packet(reply_packet, len);
    kfree(reply_packet);
}

void icmp_receive(uint8_t *packet, const uint16_t len)
{
    struct icmp_header *icmp_header =
        (struct icmp_header *)(packet + sizeof(struct ether_header) + sizeof(struct ipv4_header));

    switch (icmp_header->type) {
    case ICMP_V4_ECHO: {
        icmp_send_echo_reply(packet, len);
        icmp_send_echo_request((uint8_t[]){192, 168, 0, 1}, icmp_header->sequence);
        break;
    }
    case ICMP_REPLY: {
        // icmp_receive_echo_reply(packet, len);
        break;
    }
    default:
        break;
    }
}

void icmp_send_echo_request(const uint8_t dest_ip[static 4], const uint16_t sequence)
{
    const struct arp_cache_entry entry = arp_cache_find(dest_ip);
    if (entry.ip[0] == 0) {
        arp_send_request(dest_ip);
        return;
    }

    struct icmp_packet *packet = kzalloc(sizeof(struct icmp_packet));
    struct ether_header ether_header;
    memcpy(ether_header.dest_host, entry.mac, 6);
    memcpy(ether_header.src_host, network_get_my_mac_address(), 6);
    ether_header.ether_type = htons(ETHERTYPE_IP);

    packet->ether_header = ether_header;

    struct ipv4_header ipv4_header;
    ipv4_header.version = 4;
    ipv4_header.ihl = 0x05;
    ipv4_header.dscp_ecn = 0;
    ipv4_header.total_length =
        htons(sizeof(struct ipv4_header) + sizeof(struct icmp_header) + strlen(icmp_request_payload));
    ipv4_header.flags_fragment_offset = 0x0;
    ipv4_header.ttl = 64;
    ipv4_header.protocol = IP_PROTOCOL_ICMP;
    ipv4_header.header_checksum = 0;
    memcpy(ipv4_header.source_ip, network_get_my_ip_address(), 4);
    memcpy(ipv4_header.dest_ip, dest_ip, 4);

    ipv4_header.header_checksum = checksum((void *)&ipv4_header, ipv4_header.ihl * 4, 0);

    packet->ip_header = ipv4_header;

    struct icmp_header icmp_header;
    icmp_header.type = ICMP_V4_ECHO;
    icmp_header.code = 0;
    icmp_header.checksum = 0;
    icmp_header.id = 0;
    icmp_header.sequence = htons(sequence);
    memcpy(packet->payload, icmp_request_payload, strlen(icmp_request_payload));
    packet->icmp_header = icmp_header;

    packet->icmp_header.checksum =
        checksum(&packet->icmp_header, sizeof(struct icmp_header) + strlen(icmp_request_payload), 0);

    network_send_packet(packet, sizeof(struct icmp_packet) + strlen(icmp_request_payload));
    kfree(packet);
}
