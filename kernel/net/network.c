#include <mem/heap.h>
#include <net/arp.h>
#include <net/ethernet.h>
#include <net/icmp.h>
#include <net/ipv4.h>
#include <net/network.h>
#include <net/tcp.h>
#include <net/udp.h>
#include <lib/string.h>
#include <arpa/inet.h>
#include <stddef.h>

bool network_ready = false;
uint8_t* my_ip_address = nullptr;
uint8_t* default_gateway = nullptr;
uint8_t* subnet_mask = nullptr;
uint32_t* dns_servers;

static uint8_t* mac = nullptr;
static network_send_fn network_send;
// static network_poll_fn network_poll;

struct ether_type
{
    uint16_t ether_type;
    char* name;
};

struct ether_type ether_types[] = {
    {ETHERTYPE_PUP, "Xerox PUP"},
    {ETHERTYPE_SPRITE, "Sprite"},
    {ETHERTYPE_IP, "IPv4"},
    {ETHERTYPE_ARP, "ARP"},
    {ETHERTYPE_REVARP, "Reverse ARP"},
    {ETHERTYPE_AT, "AppleTalk protocol"},
    {ETHERTYPE_AARP, "AppleTalk ARP"},
    {ETHERTYPE_VLAN, "IEEE 802.1Q VLAN tagging"},
    {ETHERTYPE_IPX, "IPX"},
    {ETHERTYPE_IPV6, "IPv6"},
    {ETHERTYPE_LOOPBACK, "Loopback"}
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
    if (dns_server_count == 0)
    {
        if (dns_servers)
        {
            kfree(dns_servers);
            dns_servers = nullptr;
        }
        return;
    }

    auto const new_servers = (uint32_t*)kmalloc(sizeof(uint32_t) * dns_server_count);
    if (!new_servers)
        return;
    if (dns_servers)
        kfree(dns_servers);
    dns_servers = new_servers;
    memcpy(dns_servers, dns_servers_p, sizeof(uint32_t) * dns_server_count);
}

void network_set_my_ip_address(const uint8_t ip[static 4])
{
    if (my_ip_address == nullptr)
    {
        my_ip_address = (uint8_t*)kmalloc(4);
        if (!my_ip_address)
            return;
    }
    memcpy(my_ip_address, ip, 4);
}

void network_set_subnet_mask(const uint8_t ip[static 4])
{
    if (subnet_mask == nullptr)
    {
        subnet_mask = (uint8_t*)kmalloc(4);
        if (!subnet_mask)
            return;
    }
    memcpy(subnet_mask, ip, 4);
}

void network_set_default_gateway(const uint8_t ip[static 4])
{
    if (default_gateway == nullptr)
    {
        default_gateway = (uint8_t*)kmalloc(4);
        if (!default_gateway)
            return;
    }
    memcpy(default_gateway, ip, 4);
}

uint8_t* network_get_my_ip_address(void)
{
    return my_ip_address;
}

uint8_t* network_get_subnet_mask(void)
{
    return subnet_mask;
}

uint8_t* network_get_default_gateway(void)
{
    return default_gateway;
}

uint8_t* network_get_my_mac_address(void)
{
    return mac;
}

uint32_t* network_get_dns_servers(void)
{
    return dns_servers;
}

uint32_t network_get_dns_server_count(void)
{
    return dns_servers ? *dns_servers : 0;
}

const char* find_ether_type(const uint16_t ether_type)
{
    for (size_t i = 0; i < sizeof(ether_types) / sizeof(struct ether_type); i++)
    {
        if (ether_types[i].ether_type == ether_type)
        {
            return ether_types[i].name;
        }
    }
    return "Unknown";
}

void network_set_mac(const uint8_t mac_addr[static 6])
{
    if (mac == nullptr)
    {
        mac = (uint8_t*)kmalloc(6);
        if (!mac)
            return;
    }
    memcpy(mac, mac_addr, 6);
}

void network_receive(uint8_t* packet, const uint16_t len)
{
    constexpr size_t eth_len = sizeof(struct ether_header);
    if (len < eth_len) return;

    const struct ether_header* ether_header = (struct ether_header*)packet;
    const uint16_t ether_type = ntohs(ether_header->ether_type);

    switch (ether_type)
    {
    case ETHERTYPE_ARP:
        if (len < eth_len + sizeof(struct arp_header)) return;
        arp_receive(packet);
        break;
    case ETHERTYPE_IP:
        {
            if (len < eth_len + sizeof(struct ipv4_header)) return;

            auto ipv4_header = (struct ipv4_header*)(packet + eth_len);
            if (ipv4_header->version != 4 || ipv4_header->ihl < 5) return;

            const size_t ip_header_len = ipv4_header->ihl * 4;
            if (len < eth_len + ip_header_len) return;

            const size_t ip_len = ntohs(ipv4_header->total_length);
            if (ip_len < ip_header_len || ip_len > len - eth_len) return;


            const uint8_t protocol = ipv4_header->protocol;
            switch (protocol)
            {
            case IP_PROTOCOL_ICMP:
                if (my_ip_address && network_compare_ip_addresses(ipv4_header->dest_ip, my_ip_address))
                    icmp_receive(packet, len, ip_len, ip_header_len);
                break;
            case IP_PROTOCOL_TCP:
                if (my_ip_address && network_compare_ip_addresses(ipv4_header->dest_ip, my_ip_address))
                    tcp_receive(packet, len, ip_len, ip_header_len);
                break;
            case IP_PROTOCOL_UDP:
                {
                    uint32_t dest_ip = 0;
                    bytes_to_ip(ipv4_header->dest_ip, &dest_ip);
                    if (dest_ip == IP_BROADCAST_ADDRESS || (my_ip_address && network_compare_ip_addresses(
                        ipv4_header->dest_ip, my_ip_address)))
                        udp_receive(packet, len, ip_len, ip_header_len);
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

int network_send_packet(const void* data, const uint16_t len)
{
    if (!network_send) return -1;
    return network_send(data, len);
}

void network_register_driver(network_send_fn send_fn)
{
    network_send = send_fn;
    // network_poll = poll_fn;
}

void network_unregister_driver(network_send_fn send_fn)
{
    if (network_send == send_fn)
    {
        network_send = nullptr;
        // network_poll = nullptr;
    }
}

// void network_poll_rx(void)
// {
//     if (network_poll)
//     {
//         network_poll();
//     }
// }

bool network_compare_ip_addresses(const uint8_t ip1[static 4], const uint8_t ip2[static 4])
{
    if (ip1 == ip2)
    {
        return true;
    }
    return memcmp(ip1, ip2, 4) == 0;
}

bool network_compare_mac_addresses(const uint8_t mac1[static 6], const uint8_t mac2[static 6])
{
    if (mac1 == mac2)
    {
        return true;
    }
    return memcmp(mac1, mac2, 6) == 0;
}
