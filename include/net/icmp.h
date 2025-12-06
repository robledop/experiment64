#pragma once

#include <stdint.h>
#include "net/ethernet.h"
#include "net/ipv4.h"

#define ICMP_REPLY 0x00
#define ICMP_V4_ECHO 0x08

struct icmp_header
{
    uint8_t type;
    uint8_t code;
    uint16_t checksum;
    uint16_t id;
    uint16_t sequence;
} __attribute__((packed));

struct icmp_packet
{
    struct ether_header ether_header;
    struct ipv4_header ip_header;
    struct icmp_header icmp_header;
    uint8_t payload[];
} __attribute__((packed));

struct icmp_echo_reply
{
    uint16_t sequence;
    uint8_t ip[4];
};

typedef bool (*ICMP_ECHO_REPLY_CALLBACK)(struct icmp_echo_reply echo_reply);

void icmp_receive(uint8_t *packet, uint16_t len);
void icmp_send_echo_request(const uint8_t dest_ip[static 4], uint16_t sequence);
