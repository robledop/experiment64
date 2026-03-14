#pragma once

#include <stdint.h>

#define IP_PROTOCOL_ICMP 1
#define IP_PROTOCOL_TCP 6
#define IP_PROTOCOL_UDP 17

#define IP_BROADCAST_ADDRESS 0xFFFFFFFF

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


typedef struct ipv4_header ipv4_header_t;

// Pseudo-header used for TCP and UDP checksum computation (RFC 793 / 768).
struct ipv4_pseudo_header
{
    uint8_t src_ip[4];
    uint8_t dest_ip[4];
    uint8_t zero;
    uint8_t protocol;
    uint16_t length;
} __attribute__((packed));

// Fill a standard 20-byte IPv4 header and compute its checksum.
void ipv4_fill_header(struct ipv4_header *ip, uint8_t protocol,
                      const uint8_t src[4], const uint8_t dst[4],
                      uint16_t payload_len);