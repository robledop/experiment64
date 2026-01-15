#include <net/udp.h>
#include <arpa/inet.h>
#include <net/dhcp.h>
#include <net/socket.h>

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
