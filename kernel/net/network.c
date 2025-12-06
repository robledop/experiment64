#include "e1000.h"
#include "heap.h"
#include "net/arp.h"
#include "net/dhcp.h"
#include "net/ethernet.h"
#include "net/icmp.h"
#include "net/ipv4.h"
#include "net/network.h"
#include "net/udp.h"
#include "string.h"
#include <arpa/inet.h>
#include <stddef.h>

bool network_ready = false;
uint8_t *my_ip_address = nullptr;
uint8_t *default_gateway = nullptr;
uint8_t *subnet_mask = nullptr;
uint32_t *dns_servers;

static uint8_t *mac = nullptr;

struct ether_type {
    uint16_t ether_type;
    char *name;
};

struct ether_type ether_types[] = {
    {ETHERTYPE_PUP,      "Xerox PUP"               },
    {ETHERTYPE_SPRITE,   "Sprite"                  },
    {ETHERTYPE_IP,       "IPv4"                    },
    {ETHERTYPE_ARP,      "ARP"                     },
    {ETHERTYPE_REVARP,   "Reverse ARP"             },
    {ETHERTYPE_AT,       "AppleTalk protocol"      },
    {ETHERTYPE_AARP,     "AppleTalk ARP"           },
    {ETHERTYPE_VLAN,     "IEEE 802.1Q VLAN tagging"},
    {ETHERTYPE_IPX,      "IPX"                     },
    {ETHERTYPE_IPV6,     "IPv6"                    },
    {ETHERTYPE_LOOPBACK, "Loopback"                }
};

void network_set_state(bool state)
{
    network_ready = state;
}

bool network_is_ready(void)
{
    return network_ready;
}

void network_set_dns_servers(uint32_t dns_servers_p[static 1], uint32_t dns_server_count)
{
    dns_servers = (uint32_t *)kmalloc(sizeof(uint32_t) * dns_server_count);
    memcpy(dns_servers, dns_servers_p, sizeof(uint32_t) * dns_server_count);
}

void network_set_my_ip_address(const uint8_t ip[static 4])
{
    if (my_ip_address == nullptr) {
        my_ip_address = (uint8_t *)kmalloc(4);
    }
    memcpy(my_ip_address, ip, 4);
}

void network_set_subnet_mask(const uint8_t ip[static 4])
{
    if (subnet_mask == nullptr) {
        subnet_mask = (uint8_t *)kmalloc(4);
    }
    memcpy(subnet_mask, ip, 4);
}

void network_set_default_gateway(const uint8_t ip[static 4])
{
    if (default_gateway == nullptr) {
        default_gateway = (uint8_t *)kmalloc(4);
    }
    memcpy(default_gateway, ip, 4);
}

uint8_t *network_get_my_ip_address(void)
{
    return my_ip_address;
}

uint8_t *network_get_my_mac_address(void)
{
    return mac;
}

const char *find_ether_type(const uint16_t ether_type)
{
    for (size_t i = 0; i < sizeof(ether_types) / sizeof(struct ether_type); i++) {
        if (ether_types[i].ether_type == ether_type) {
            return ether_types[i].name;
        }
    }
    return "Unknown";
}

void network_set_mac(const uint8_t mac_addr[static 6])
{
    if (mac == nullptr) {
        mac = (uint8_t *)kmalloc(6);
    }
    memcpy(mac, mac_addr, 6);
}

void network_receive(uint8_t *packet, const uint16_t len)
{
    const struct ether_header *ether_header = (struct ether_header *)packet;
    const uint16_t ether_type = ntohs(ether_header->ether_type);

    switch (ether_type) {
    case ETHERTYPE_ARP:
        arp_receive(packet);
        break;
    case ETHERTYPE_IP: {
        struct ipv4_header *ipv4_header = (struct ipv4_header *)(packet + sizeof(struct ether_header));
        const uint8_t protocol = ipv4_header->protocol;
        switch (protocol) {
        case IP_PROTOCOL_ICMP:
            if (my_ip_address && network_compare_ip_addresses(ipv4_header->dest_ip, my_ip_address)) {
                icmp_receive(packet, len);
            }
            break;
        case IP_PROTOCOL_TCP:
            break;
        case IP_PROTOCOL_UDP: {
            struct udp_header *udp_header = (struct udp_header *)(packet + sizeof(struct ether_header) + sizeof(struct ipv4_header));
            if (udp_header->dest_port == htons(DHCP_SOURCE_PORT)) {
                dhcp_receive(packet);
            }
            break;
        }
        default:
            break;
        }
        break;
    }
    default:
        break;
    }
}

int network_send_packet(const void *data, const uint16_t len)
{
    return e1000_send_packet(data, len);
}

bool network_compare_ip_addresses(const uint8_t ip1[static 4], const uint8_t ip2[static 4])
{
    if (ip1 == ip2) {
        return true;
    }
    return memcmp(ip1, ip2, 4) == 0;
}

bool network_compare_mac_addresses(const uint8_t mac1[static 6], const uint8_t mac2[static 6])
{
    if (mac1 == mac2) {
        return true;
    }
    return memcmp(mac1, mac2, 6) == 0;
}
