#pragma once

#include <stdint.h>
#include "net/ethernet.h"

#define ARP_REQUEST 1
#define ARP_REPLY 2

#define ARP_CACHE_SIZE 256
#define ARP_CACHE_TIMEOUT 60000 // ticks

struct arp_header
{
    uint16_t hw_type;
    uint16_t protocol_type;
    uint8_t hw_addr_len;
    uint8_t protocol_addr_len;
    uint16_t opcode;
    uint8_t sender_hw_addr[6];
    uint8_t sender_protocol_addr[4];
    uint8_t target_hw_addr[6];
    uint8_t target_protocol_addr[4];
};

struct arp_packet
{
    struct ether_header ether_header;
    struct arp_header arp_packet;
} __attribute__((packed));

struct arp_cache_entry
{
    uint8_t ip[4];
    uint8_t mac[6];
    uint32_t timestamp;
};

struct arp_cache_entry arp_cache_find(const uint8_t ip[static 4]);
void arp_receive(uint8_t *packet);
void arp_send_reply(uint8_t *packet);
void arp_send_request(const uint8_t dest_ip[static 4]);
void arp_init(void);
