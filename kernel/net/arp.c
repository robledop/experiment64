#include "net/arp.h"
#include "net/ethernet.h"
#include "net/helpers.h"
#include "net/network.h"
#include "heap.h"
#include "process.h"
#include "string.h"

uint8_t broadcast_mac[6] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};

struct arp_cache_entry *arp_cache;

void arp_init(void)
{
    arp_cache = kzalloc(sizeof(struct arp_cache_entry) * ARP_CACHE_SIZE);
}

void arp_cache_remove_expired_entries(void)
{
    const uint32_t current_time = scheduler_ticks;
    for (int i = 0; i < ARP_CACHE_SIZE; i++) {
        if (arp_cache[i].ip[0] != 0 && current_time - arp_cache[i].timestamp > ARP_CACHE_TIMEOUT) {
            memset(arp_cache[i].ip, 0, 4);
            memset(arp_cache[i].mac, 0, 6);
            arp_cache[i].timestamp = 0;
        }
    }
}

struct arp_cache_entry arp_cache_find(const uint8_t ip[static 4])
{
    arp_cache_remove_expired_entries();

    for (int i = 0; i < ARP_CACHE_SIZE; i++) {
        if (network_compare_ip_addresses(arp_cache[i].ip, ip)) {
            return arp_cache[i];
        }
    }
    return (struct arp_cache_entry){0};
}

void arp_cache_add(uint8_t ip[static 4], uint8_t mac[static 6])
{
    for (int i = 0; i < ARP_CACHE_SIZE; i++) {
        if (arp_cache[i].ip[0] == 0) {
            memcpy(arp_cache[i].ip, ip, 4);
            memcpy(arp_cache[i].mac, mac, 6);
            arp_cache[i].timestamp = scheduler_ticks;
            return;
        }
    }
}

void arp_receive_reply(uint8_t *packet)
{
    struct arp_header *arp_header = (struct arp_header *)(packet + sizeof(struct ether_header));

    struct arp_cache_entry cache_entry = arp_cache_find(arp_header->sender_protocol_addr);
    if (cache_entry.ip[0] == 0) {
        arp_cache_add(arp_header->sender_protocol_addr, arp_header->sender_hw_addr);
    }
}

void arp_receive(uint8_t *packet)
{
    const struct arp_header *arp = (struct arp_header *)(packet + sizeof(struct ether_header));
    const uint16_t opcode = ntohs(arp->opcode);
    switch (opcode) {
    case ARP_REQUEST:
        if (network_get_my_ip_address() &&
            network_compare_ip_addresses(arp->target_protocol_addr, network_get_my_ip_address())) {
            arp_send_reply(packet);
        }
        break;
    case ARP_REPLY:
        if (network_get_my_ip_address() &&
            network_compare_ip_addresses(arp->target_protocol_addr, network_get_my_ip_address())) {
            arp_receive_reply(packet);
        }
        break;
    default:
        break;
    }
}

void arp_send_request(const uint8_t dest_ip[static 4])
{
    struct arp_packet *packet = kzalloc(sizeof(struct arp_packet));
    struct ether_header ether_header;
    memcpy(ether_header.dest_host, broadcast_mac, 6);
    memcpy(ether_header.src_host, network_get_my_mac_address(), 6);
    ether_header.ether_type = htons(ETHERTYPE_ARP);

    packet->ether_header = ether_header;

    struct arp_header arp_header;
    arp_header.hw_type = htons(1);
    arp_header.protocol_type = htons(ETHERTYPE_IP);
    arp_header.hw_addr_len = 6;
    arp_header.protocol_addr_len = 4;
    arp_header.opcode = htons(ARP_REQUEST);
    memcpy(arp_header.sender_hw_addr, network_get_my_mac_address(), 6);
    memcpy(arp_header.sender_protocol_addr, network_get_my_ip_address(), 4);
    memcpy(arp_header.target_hw_addr, broadcast_mac, 6);
    memcpy(arp_header.target_protocol_addr, dest_ip, 4);

    packet->arp_packet = arp_header;

    network_send_packet(packet, sizeof(struct arp_packet));
    kfree(packet);
}

void arp_send_reply(uint8_t *packet)
{
    const struct ether_header *ether_header = (struct ether_header *)packet;
    const struct arp_header *arp_header = (struct arp_header *)(packet + sizeof(struct ether_header));

    struct ether_header reply_ether_header;
    memcpy(reply_ether_header.dest_host, ether_header->src_host, 6);
    memcpy(reply_ether_header.src_host, network_get_my_mac_address(), 6);
    reply_ether_header.ether_type = htons(ETHERTYPE_ARP);

    struct arp_header reply_arp_header;
    reply_arp_header.hw_type = arp_header->hw_type;
    reply_arp_header.protocol_type = arp_header->protocol_type;
    reply_arp_header.hw_addr_len = arp_header->hw_addr_len;
    reply_arp_header.protocol_addr_len = arp_header->protocol_addr_len;
    reply_arp_header.opcode = htons(ARP_REPLY);
    memcpy(reply_arp_header.sender_hw_addr, network_get_my_mac_address(), 6);
    memcpy(reply_arp_header.sender_protocol_addr, network_get_my_ip_address(), 4);
    memcpy(reply_arp_header.target_hw_addr, arp_header->sender_hw_addr, 6);
    memcpy(reply_arp_header.target_protocol_addr, arp_header->sender_protocol_addr, 4);

    struct arp_packet *reply_packet = kzalloc(sizeof(struct arp_packet));

    reply_packet->ether_header = reply_ether_header;
    reply_packet->arp_packet = reply_arp_header;

    network_send_packet(reply_packet, sizeof(struct arp_packet));
    kfree(reply_packet);
}
