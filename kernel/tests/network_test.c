#include "test.h"
#include "net/helpers.h"
#include "net/network.h"
#include "net/arp.h"
#include "net/dhcp.h"
#include "net/ethernet.h"
#include "net/ipv4.h"
#include "net/udp.h"
#include "string.h"
#include <arpa/inet.h>

// ============================================================================
// Byte order conversion tests
// ============================================================================

TEST(test_htons)
{
    // 0x1234 in host order should become 0x3412 in network order (big-endian)
    TEST_ASSERT(htons(0x1234) == 0x3412);
    TEST_ASSERT(htons(0x0000) == 0x0000);
    TEST_ASSERT(htons(0xFFFF) == 0xFFFF);
    TEST_ASSERT(htons(0x00FF) == 0xFF00);
    return true;
}

TEST(test_ntohs)
{
    // ntohs should be the inverse of htons
    TEST_ASSERT(ntohs(0x3412) == 0x1234);
    TEST_ASSERT(ntohs(htons(0xABCD)) == 0xABCD);
    TEST_ASSERT(ntohs(0x0000) == 0x0000);
    return true;
}

TEST(test_htonl)
{
    // 0x12345678 should become 0x78563412
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
    uint32_t test_vals[] = {0, 1, 255, 256, 65535, 65536, 0x12345678, 0xDEADBEEF};
    for (size_t i = 0; i < sizeof(test_vals) / sizeof(test_vals[0]); i++) {
        uint32_t v = test_vals[i];
        TEST_ASSERT(htonl(htonl(v)) == v);
    }
    return true;
}

// ============================================================================
// Checksum tests
// ============================================================================

TEST(test_checksum_zeros)
{
    uint8_t data[10] = {0};
    uint16_t cs = checksum(data, sizeof(data), 0);
    // Checksum of all zeros should be 0xFFFF (ones complement of 0)
    TEST_ASSERT(cs == 0xFFFF);
    return true;
}

TEST(test_checksum_ones)
{
    uint8_t data[4] = {0xFF, 0xFF, 0xFF, 0xFF};
    uint16_t cs = checksum(data, sizeof(data), 0);
    // Checksum should be 0 (ones complement of 0xFFFF + 0xFFFF with carry)
    TEST_ASSERT(cs == 0x0000);
    return true;
}

TEST(test_checksum_simple)
{
    // On little-endian x86: {0x00, 0x01} reads as 0x0100, {0x00, 0x02} reads as 0x0200
    // Sum = 0x0100 + 0x0200 = 0x0300, ~0x0300 = 0xFCFF
    uint8_t data[4] = {0x00, 0x01, 0x00, 0x02};
    uint16_t cs = checksum(data, sizeof(data), 0);
    TEST_ASSERT(cs == 0xFCFF);
    return true;
}

TEST(test_checksum_odd_length)
{
    // {0x00, 0x01} reads as 0x0100, then odd byte 0x02
    // Sum = 0x0100 + 0x02 = 0x0102, ~0x0102 = 0xFEFD
    uint8_t data[3] = {0x00, 0x01, 0x02};
    uint16_t cs = checksum(data, sizeof(data), 0);
    TEST_ASSERT(cs == 0xFEFD);
    return true;
}

// ============================================================================
// IP/MAC comparison tests
// ============================================================================

TEST(test_compare_ip_addresses_equal)
{
    uint8_t ip1[4] = {192, 168, 1, 1};
    uint8_t ip2[4] = {192, 168, 1, 1};
    TEST_ASSERT(network_compare_ip_addresses(ip1, ip2) == true);
    return true;
}

TEST(test_compare_ip_addresses_not_equal)
{
    uint8_t ip1[4] = {192, 168, 1, 1};
    uint8_t ip2[4] = {192, 168, 1, 2};
    TEST_ASSERT(network_compare_ip_addresses(ip1, ip2) == false);
    return true;
}

TEST(test_compare_ip_addresses_same_pointer)
{
    uint8_t ip[4] = {10, 0, 0, 1};
    TEST_ASSERT(network_compare_ip_addresses(ip, ip) == true);
    return true;
}

TEST(test_compare_mac_addresses_equal)
{
    uint8_t mac1[6] = {0x52, 0x54, 0x00, 0x12, 0x34, 0x56};
    uint8_t mac2[6] = {0x52, 0x54, 0x00, 0x12, 0x34, 0x56};
    TEST_ASSERT(network_compare_mac_addresses(mac1, mac2) == true);
    return true;
}

TEST(test_compare_mac_addresses_not_equal)
{
    uint8_t mac1[6] = {0x52, 0x54, 0x00, 0x12, 0x34, 0x56};
    uint8_t mac2[6] = {0x52, 0x54, 0x00, 0x12, 0x34, 0x57};
    TEST_ASSERT(network_compare_mac_addresses(mac1, mac2) == false);
    return true;
}

TEST(test_compare_mac_addresses_broadcast)
{
    uint8_t broadcast1[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    uint8_t broadcast2[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    TEST_ASSERT(network_compare_mac_addresses(broadcast1, broadcast2) == true);
    return true;
}

// ============================================================================
// MAC address string formatting tests
// ============================================================================

TEST(test_get_mac_address_string)
{
    uint8_t mac[6] = {0x52, 0x54, 0x00, 0x12, 0x34, 0x56};
    char *result = get_mac_address_string(mac);
    TEST_ASSERT(strcmp(result, "52:54:00:12:34:56") == 0);
    return true;
}

TEST(test_get_mac_address_string_zeros)
{
    uint8_t mac[6] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    char *result = get_mac_address_string(mac);
    TEST_ASSERT(strcmp(result, "00:00:00:00:00:00") == 0);
    return true;
}

TEST(test_get_mac_address_string_broadcast)
{
    uint8_t mac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    char *result = get_mac_address_string(mac);
    TEST_ASSERT(strcmp(result, "FF:FF:FF:FF:FF:FF") == 0);
    return true;
}

// ============================================================================
// IP string conversion tests
// ============================================================================

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
    // Invalid characters in middle octets should return 0
    TEST_ASSERT(inet_addr("1.a.3.4") == 0);
    return true;
}

// ============================================================================
// DHCP options parsing tests
// ============================================================================

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

    uint32_t mask = dhcp_options_get_ip_option(options, DHCP_OPT_SUBNET_MASK);
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

    uint32_t router = dhcp_options_get_ip_option(options, DHCP_OPT_ROUTER);
    TEST_ASSERT(router == 0xC0A80101);
    return true;
}

TEST(test_dhcp_options_get_ip_option_not_found)
{
    uint8_t options[DHCP_OPTIONS_LEN] = {0};
    options[0] = DHCP_OPT_END;

    uint32_t result = dhcp_options_get_ip_option(options, DHCP_OPT_ROUTER);
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
    int result = dhcp_options_get_dns_servers(options, dns_servers, &count);

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

// ============================================================================
// Ethernet header tests
// ============================================================================

TEST(test_ether_header_size)
{
    // Ethernet header should be exactly 14 bytes
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

// ============================================================================
// IPv4 header tests
// ============================================================================

TEST(test_ipv4_header_size)
{
    // IPv4 header (without options) should be 20 bytes
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

// ============================================================================
// UDP header tests
// ============================================================================

TEST(test_udp_header_size)
{
    TEST_ASSERT(sizeof(struct udp_header) == 8);
    return true;
}

// ============================================================================
// DHCP constants tests
// ============================================================================

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

