#include <tests/test.h>
#include <net/arp.h>
#include <lib/string.h>

TEST(test_arp_cache_add_and_find)
{
    arp_init(); // Ensure cache is allocated

    uint8_t ip[4] = {10, 0, 2, 100};
    uint8_t mac[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};

    arp_cache_add(ip, mac);

    struct arp_cache_entry entry = arp_cache_find(ip);
    TEST_ASSERT(memcmp(entry.ip, ip, 4) == 0);
    TEST_ASSERT(memcmp(entry.mac, mac, 6) == 0);
    return true;
}

TEST(test_arp_cache_find_miss)
{
    arp_init();

    uint8_t ip[4] = {10, 0, 2, 201};
    struct arp_cache_entry entry = arp_cache_find(ip);

    // A miss should return a zeroed MAC.
    uint8_t zero_mac[6] = {0};
    TEST_ASSERT(memcmp(entry.mac, zero_mac, 6) == 0);
    return true;
}

TEST(test_arp_cache_multiple_entries)
{
    arp_init();

    uint8_t ip1[4] = {10, 0, 2, 103};
    uint8_t mac1[6] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66};
    uint8_t ip2[4] = {10, 0, 2, 104};
    uint8_t mac2[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};

    arp_cache_add(ip1, mac1);
    arp_cache_add(ip2, mac2);

    struct arp_cache_entry e1 = arp_cache_find(ip1);
    TEST_ASSERT(memcmp(e1.mac, mac1, 6) == 0);

    struct arp_cache_entry e2 = arp_cache_find(ip2);
    TEST_ASSERT(memcmp(e2.mac, mac2, 6) == 0);
    return true;
}
