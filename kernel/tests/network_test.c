#include <tests/test.h>
#include "net/helpers.h"
#include "net/network.h"
#include "net/dhcp.h"
#include "net/ethernet.h"
#include "net/ipv4.h"
#include "net/tcp.h"
#include "net/udp.h"
#include "net/socket.h"
#include <mem/heap.h>
#include <lib/string.h>
#include <arpa/inet.h>

TEST(test_htons)
{
    TEST_ASSERT(htons(0x1234) == 0x3412);
    TEST_ASSERT(htons(0x0000) == 0x0000);
    TEST_ASSERT(htons(0xFFFF) == 0xFFFF);
    TEST_ASSERT(htons(0x00FF) == 0xFF00);
    return true;
}

TEST(test_ntohs)
{
    TEST_ASSERT(ntohs(0x3412) == 0x1234);
    TEST_ASSERT(ntohs(htons(0xABCD)) == 0xABCD);
    TEST_ASSERT(ntohs(0x0000) == 0x0000);
    return true;
}

TEST(test_htonl)
{
    TEST_ASSERT(htonl(0x12345678) == 0x78563412);
    TEST_ASSERT(htonl(0x00000000) == 0x00000000);
    TEST_ASSERT(htonl(0xFFFFFFFF) == 0xFFFFFFFF);
    TEST_ASSERT(htonl(0x000000FF) == 0xFF000000);
    return true;
}

TEST(test_byte_order_roundtrip)
{
    // htons/ntohs should be inverses
    for (uint16_t v = 0; v < 1000; v++) {
        TEST_ASSERT(ntohs(htons(v)) == v);
    }
    // htonl roundtrip
    constexpr uint32_t test_vals[] = {0, 1, 255, 256, 65535, 65536, 0x12345678, 0xDEADBEEF};
    for (size_t i = 0; i < sizeof(test_vals) / sizeof(test_vals[0]); i++) {
        uint32_t v = test_vals[i];
        TEST_ASSERT(htonl(htonl(v)) == v);
    }
    return true;
}

TEST(test_checksum_zeros)
{
    uint8_t data[10] = {0};
    const uint16_t cs = checksum(data, sizeof(data), 0);
    // Checksum of all zeros should be 0xFFFF (one's complement of 0)
    TEST_ASSERT(cs == 0xFFFF);
    return true;
}

TEST(test_checksum_ones)
{
    uint8_t data[4] = {0xFF, 0xFF, 0xFF, 0xFF};
    const uint16_t cs = checksum(data, sizeof(data), 0);
    // Checksum should be 0 (one's complement of 0xFFFF + 0xFFFF with carry)
    TEST_ASSERT(cs == 0x0000);
    return true;
}

TEST(test_checksum_simple)
{
    // On little-endian x86: {0x00, 0x01} reads as 0x0100, {0x00, 0x02} reads as 0x0200
    // Sum = 0x0100 + 0x0200 = 0x0300, ~0x0300 = 0xFCFF
    uint8_t data[4] = {0x00, 0x01, 0x00, 0x02};
    const uint16_t cs = checksum(data, sizeof(data), 0);
    TEST_ASSERT(cs == 0xFCFF);
    return true;
}

TEST(test_checksum_odd_length)
{
    // {0x00, 0x01} reads as 0x0100, then odd byte 0x02
    // Sum = 0x0100 + 0x02 = 0x0102, ~0x0102 = 0xFEFD
    uint8_t data[3] = {0x00, 0x01, 0x02};
    const uint16_t cs = checksum(data, sizeof(data), 0);
    TEST_ASSERT(cs == 0xFEFD);
    return true;
}

TEST(test_compare_ip_addresses_equal)
{
    constexpr uint8_t ip1[4] = {192, 168, 1, 1};
    constexpr uint8_t ip2[4] = {192, 168, 1, 1};
    TEST_ASSERT(network_compare_ip_addresses(ip1, ip2) == true);
    return true;
}

TEST(test_compare_ip_addresses_not_equal)
{
    constexpr uint8_t ip1[4] = {192, 168, 1, 1};
    constexpr uint8_t ip2[4] = {192, 168, 1, 2};
    TEST_ASSERT(network_compare_ip_addresses(ip1, ip2) == false);
    return true;
}

TEST(test_compare_ip_addresses_same_pointer)
{
    constexpr uint8_t ip[4] = {10, 0, 0, 1};
    TEST_ASSERT(network_compare_ip_addresses(ip, ip) == true);
    return true;
}

TEST(test_compare_mac_addresses_equal)
{
    const uint8_t mac1[6] = {0x52, 0x54, 0x00, 0x12, 0x34, 0x56};
    const uint8_t mac2[6] = {0x52, 0x54, 0x00, 0x12, 0x34, 0x56};
    TEST_ASSERT(network_compare_mac_addresses(mac1, mac2) == true);
    return true;
}

TEST(test_compare_mac_addresses_not_equal)
{
    const uint8_t mac1[6] = {0x52, 0x54, 0x00, 0x12, 0x34, 0x56};
    const uint8_t mac2[6] = {0x52, 0x54, 0x00, 0x12, 0x34, 0x57};
    TEST_ASSERT(network_compare_mac_addresses(mac1, mac2) == false);
    return true;
}

TEST(test_compare_mac_addresses_broadcast)
{
    const uint8_t broadcast1[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    const uint8_t broadcast2[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    TEST_ASSERT(network_compare_mac_addresses(broadcast1, broadcast2) == true);
    return true;
}

TEST(test_get_mac_address_string)
{
    const uint8_t mac[6] = {0x52, 0x54, 0x00, 0x12, 0x34, 0x56};
    const char *result = get_mac_address_string(mac);
    TEST_ASSERT(strcmp(result, "52:54:00:12:34:56") == 0);
    return true;
}

TEST(test_get_mac_address_string_zeros)
{
    const uint8_t mac[6] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    const char *result = get_mac_address_string(mac);
    TEST_ASSERT(strcmp(result, "00:00:00:00:00:00") == 0);
    return true;
}

TEST(test_get_mac_address_string_broadcast)
{
    const uint8_t mac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    const char *result = get_mac_address_string(mac);
    TEST_ASSERT(strcmp(result, "FF:FF:FF:FF:FF:FF") == 0);
    return true;
}

TEST(test_inet_addr)
{
    TEST_ASSERT(inet_addr("0.0.0.0") == 0);
    TEST_ASSERT(inet_addr("0.0.0.1") == 1);
    TEST_ASSERT(inet_addr("0.0.1.0") == 256);
    TEST_ASSERT(inet_addr("192.168.1.1") == 0xC0A80101);
    TEST_ASSERT(inet_addr("255.255.255.255") == 0xFFFFFFFF);
    return true;
}

TEST(test_inet_addr_invalid)
{
    // Values >= 256 should return 0
    TEST_ASSERT(inet_addr("256.0.0.0") == 0);
    TEST_ASSERT(inet_addr("0.256.0.0") == 0);
    // Invalid characters in the middle octets should return 0
    TEST_ASSERT(inet_addr("1.a.3.4") == 0);
    return true;
}

TEST(test_dhcp_options_get_ip_option_subnet)
{
    uint8_t options[DHCP_OPTIONS_LEN] = {0};
    // Build options: Message type (53), then subnet mask (1)
    options[0] = 53;  // DHCP message type
    options[1] = 1;   // length
    options[2] = 5;   // ACK
    options[3] = DHCP_OPT_SUBNET_MASK;
    options[4] = 4;   // length
    options[5] = 255;
    options[6] = 255;
    options[7] = 255;
    options[8] = 0;
    options[9] = DHCP_OPT_END;

    const uint32_t mask = dhcp_options_get_ip_option(options, DHCP_OPT_SUBNET_MASK);
    // Should return 255.255.255.0 in big-endian form as uint32
    TEST_ASSERT(mask == 0xFFFFFF00);
    return true;
}

TEST(test_dhcp_options_get_ip_option_router)
{
    uint8_t options[DHCP_OPTIONS_LEN] = {0};
    options[0] = DHCP_OPT_ROUTER;
    options[1] = 4;
    options[2] = 192;
    options[3] = 168;
    options[4] = 1;
    options[5] = 1;
    options[6] = DHCP_OPT_END;

    const uint32_t router = dhcp_options_get_ip_option(options, DHCP_OPT_ROUTER);
    TEST_ASSERT(router == 0xC0A80101);
    return true;
}

TEST(test_dhcp_options_get_ip_option_not_found)
{
    uint8_t options[DHCP_OPTIONS_LEN] = {0};
    options[0] = DHCP_OPT_END;

    const uint32_t result = dhcp_options_get_ip_option(options, DHCP_OPT_ROUTER);
    TEST_ASSERT(result == 0);
    return true;
}

TEST(test_dhcp_options_get_dns_servers)
{
    uint8_t options[DHCP_OPTIONS_LEN] = {0};
    options[0] = DHCP_OPT_DNS;
    options[1] = 8;  // 2 DNS servers
    // DNS 1: 8.8.8.8
    options[2] = 8;
    options[3] = 8;
    options[4] = 8;
    options[5] = 8;
    // DNS 2: 8.8.4.4
    options[6] = 8;
    options[7] = 8;
    options[8] = 4;
    options[9] = 4;
    options[10] = DHCP_OPT_END;

    uint32_t dns_servers[5];
    size_t count = 0;
    const int result = dhcp_options_get_dns_servers(options, dns_servers, &count);

    TEST_ASSERT(result == 0);
    TEST_ASSERT(count == 2);
    return true;
}

TEST(test_dhcp_options_get_dns_servers_none)
{
    uint8_t options[DHCP_OPTIONS_LEN] = {0};
    options[0] = DHCP_OPT_END;

    uint32_t dns_servers[5];
    size_t count = 99;
    int result = dhcp_options_get_dns_servers(options, dns_servers, &count);

    TEST_ASSERT(result == -1);  // Not found
    TEST_ASSERT(count == 0);
    return true;
}

TEST(test_dhcp_options_with_padding)
{
    uint8_t options[DHCP_OPTIONS_LEN] = {0};
    options[0] = DHCP_OPT_PAD;
    options[1] = DHCP_OPT_PAD;
    options[2] = DHCP_OPT_PAD;
    options[3] = DHCP_OPT_ROUTER;
    options[4] = 4;
    options[5] = 10;
    options[6] = 0;
    options[7] = 0;
    options[8] = 1;
    options[9] = DHCP_OPT_END;

    uint32_t router = dhcp_options_get_ip_option(options, DHCP_OPT_ROUTER);
    TEST_ASSERT(router == 0x0A000001);
    return true;
}

TEST(test_ether_header_size)
{
    TEST_ASSERT(sizeof(struct ether_header) == 14);
    return true;
}

TEST(test_ether_type_constants)
{
    TEST_ASSERT(ETHERTYPE_IP == 0x0800);
    TEST_ASSERT(ETHERTYPE_ARP == 0x0806);
    TEST_ASSERT(ETHERTYPE_IPV6 == 0x86dd);
    return true;
}

TEST(test_ipv4_header_size)
{
    TEST_ASSERT(sizeof(struct ipv4_header) == 20);
    return true;
}

TEST(test_ip_protocol_constants)
{
    TEST_ASSERT(IP_PROTOCOL_ICMP == 1);
    TEST_ASSERT(IP_PROTOCOL_TCP == 6);
    TEST_ASSERT(IP_PROTOCOL_UDP == 17);
    return true;
}

TEST(test_udp_header_size)
{
    TEST_ASSERT(sizeof(struct udp_header) == 8);
    return true;
}

TEST(test_dhcp_constants)
{
    TEST_ASSERT(DHCP_SOURCE_PORT == 68);
    TEST_ASSERT(DHCP_DEST_PORT == 67);
    TEST_ASSERT(DHCP_MAGIC_COOKIE == 0x63825363);
    return true;
}

TEST(test_dhcp_header_size)
{
    // DHCP header size calculation:
    // op(1) + htype(1) + hlen(1) + hops(1) + xid(4) + secs(2) + flags(2) +
    // ciaddr(4) + yiaddr(4) + siaddr(4) + giaddr(4) + chaddr(6) + reserved(10) +
    // sname(64) + file(128) + magic(4) + options(128) = 368 bytes
    TEST_ASSERT(sizeof(struct dhcp_header) == 368);
    return true;
}

static bool tcp_test_build_packet(uint8_t* packet, const size_t packet_len,
                                  const uint8_t src_mac[static 6], const uint8_t dest_mac[static 6],
                                  const uint8_t src_ip[static 4], const uint8_t dest_ip[static 4],
                                  const uint16_t src_port, const uint16_t dest_port,
                                  const uint32_t seq_num, const uint32_t ack_num, const uint8_t flags,
                                  const uint8_t* payload, const size_t payload_len)
{
    constexpr size_t eth_len = sizeof(struct ether_header);
    constexpr size_t ip_header_len = sizeof(struct ipv4_header);
    constexpr size_t tcp_header_len = sizeof(struct tcp_header);
    const size_t ip_len = ip_header_len + tcp_header_len + payload_len;
    if (packet_len < eth_len + ip_len)
        return false;

    memset(packet, 0, packet_len);

    auto const eth = (struct ether_header*)packet;
    memcpy(eth->dest_host, dest_mac, 6);
    memcpy(eth->src_host, src_mac, 6);
    eth->ether_type = htons(ETHERTYPE_IP);

    auto const ip = (struct ipv4_header*)(packet + eth_len);
    ip->version = 4;
    ip->ihl = (uint8_t)(ip_header_len / 4);
    ip->dscp_ecn = 0;
    ip->total_length = htons((uint16_t)ip_len);
    ip->identification = 0;
    ip->flags_fragment_offset = 0;
    ip->ttl = 64;
    ip->protocol = IP_PROTOCOL_TCP;
    ip->header_checksum = 0;
    memcpy(ip->source_ip, src_ip, 4);
    memcpy(ip->dest_ip, dest_ip, 4);

    auto const tcp = (struct tcp_header*)(packet + eth_len + ip_header_len);
    tcp->src_port = src_port;
    tcp->dst_port = dest_port;
    tcp->seq_num = htonl(seq_num);
    tcp->ack_num = htonl(ack_num);
    tcp->data_offset_reserved = (uint8_t)((tcp_header_len / 4) << 4);
    tcp->flags = flags;
    tcp->window = htons(4096);
    tcp->checksum = 0;
    tcp->urgent_ptr = 0;

    if (payload_len > 0)
        memcpy(packet + eth_len + ip_header_len + tcp_header_len, payload, payload_len);
    return true;
}

TEST(test_tcp_receive_basic)
{
    constexpr uint8_t my_ip[4] = {10, 0, 2, 15};
    const uint8_t my_mac[6] = {0x52, 0x54, 0x00, 0x12, 0x34, 0x56};
    constexpr uint8_t remote_ip[4] = {10, 0, 2, 2};
    const uint8_t remote_mac[6] = {0x52, 0x54, 0x00, 0xAB, 0xCD, 0xEF};
    const uint16_t local_port = htons(8080);
    const uint16_t remote_port = htons(12345);
    constexpr uint32_t remote_seq = 1000;

    network_set_my_ip_address(my_ip);
    network_set_mac(my_mac);

    socket_t sock = {};
    sock.domain = AF_INET;
    sock.type = SOCK_STREAM;
    sock.protocol = IPPROTO_TCP;
    sock.state = SOCKET_STATE_LISTENING;
    sock.local.port = local_port;
    socket_register(&sock);

    constexpr size_t ip_header_len = sizeof(struct ipv4_header);

    constexpr size_t syn_ip_len = ip_header_len + sizeof(struct tcp_header);
    constexpr size_t syn_len = sizeof(struct ether_header) + syn_ip_len;
    uint8_t syn_packet[syn_len];
    TEST_ASSERT(tcp_test_build_packet(syn_packet, sizeof(syn_packet),
                                      remote_mac, my_mac,
                                      remote_ip, my_ip,
                                      remote_port, local_port,
                                      remote_seq, 0, TCP_FLAG_SYN,
                                      nullptr, 0));
    tcp_receive(syn_packet, (uint16_t)sizeof(syn_packet), syn_ip_len, ip_header_len);

    TEST_ASSERT(sock.state == SOCKET_STATE_LISTENING);
    socket_t* child = socket_find_tcp_connected(my_ip, local_port, remote_ip, remote_port);
    TEST_ASSERT(child != nullptr);
    TEST_ASSERT((child->flags & SOCKET_FLAG_TCP_SYN_RCVD) != 0);
    TEST_ASSERT(child->tcp_recv_next == remote_seq + 1);

    constexpr size_t ack_ip_len = ip_header_len + sizeof(struct tcp_header);
    constexpr size_t ack_len = sizeof(struct ether_header) + ack_ip_len;
    uint8_t ack_packet[ack_len];
    TEST_ASSERT(tcp_test_build_packet(ack_packet, sizeof(ack_packet),
                                      remote_mac, my_mac,
                                      remote_ip, my_ip,
                                      remote_port, local_port,
                                      remote_seq + 1, child->tcp_send_next, TCP_FLAG_ACK,
                                      nullptr, 0));
    tcp_receive(ack_packet, (uint16_t)sizeof(ack_packet), ack_ip_len, ip_header_len);

    TEST_ASSERT((child->flags & SOCKET_FLAG_TCP_ESTABLISHED) != 0);

    constexpr char payload[] = "hi";
    constexpr size_t data_ip_len = ip_header_len + sizeof(struct tcp_header) + sizeof(payload) - 1;
    constexpr size_t data_len = sizeof(struct ether_header) + data_ip_len;
    uint8_t data_packet[data_len];
    TEST_ASSERT(tcp_test_build_packet(data_packet, sizeof(data_packet),
                                      remote_mac, my_mac,
                                      remote_ip, my_ip,
                                      remote_port, local_port,
                                      child->tcp_recv_next, child->tcp_send_next,
                                      (uint8_t)(TCP_FLAG_ACK | TCP_FLAG_PSH),
                                      (const uint8_t*)payload, sizeof(payload) - 1));
    tcp_receive(data_packet, (uint16_t)sizeof(data_packet), data_ip_len, ip_header_len);

    socket_rx_packet_t* pkt = socket_rx_pop(child, false);
    TEST_ASSERT(pkt != nullptr);
    TEST_ASSERT(pkt->len == sizeof(payload) - 1);
    TEST_ASSERT(memcmp(pkt->data, payload, sizeof(payload) - 1) == 0);
    if (pkt->data)
        kfree(pkt->data);
    kfree(pkt);

    socket_unregister(&sock);
    return true;
}
