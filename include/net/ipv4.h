#pragma once

#include <stdint.h>

#define IP_PROTOCOL_ICMP 1
#define IP_PROTOCOL_TCP 6
#define IP_PROTOCOL_UDP 17

struct ipv4_header
{
    // 4 bits IHL (Internet Header Length)
    uint8_t ihl : 4;
    // 4 bits version (always 4),
    uint8_t version : 4;
    // 6 bits Differentiated Services Code Point (DSCP)
    // 2 bits Explicit Congestion Notification (ECN)
    uint8_t dscp_ecn;
    // Total length of the entire packet
    uint16_t total_length;
    uint16_t identification;
    // 3 bits flags, 13 bits fragment offset
    uint16_t flags_fragment_offset;
    // Time to live
    uint8_t ttl;
    // Protocol (TCP, UDP, ICMP, etc)
    uint8_t protocol;
    uint16_t header_checksum;
    uint8_t source_ip[4];
    uint8_t dest_ip[4];
} __attribute__((packed));
