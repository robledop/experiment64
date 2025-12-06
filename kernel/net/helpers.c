#include "net/helpers.h"

uint16_t checksum(void *addr, int count, const int start_sum)
{
    uint32_t sum = start_sum;
    const uint8_t *ptr = (const uint8_t *)addr;

    while (count > 1) {
        uint16_t word = (uint16_t)ptr[0] | ((uint16_t)ptr[1] << 8);
        sum += word;
        ptr += 2;
        count -= 2;
    }

    if (count > 0) {
        sum += (uint16_t)ptr[0];
    }

    while (sum >> 16)
        sum = (sum & 0xffff) + (sum >> 16);

    return ~sum;
}

char *get_mac_address_string(uint8_t mac[6])
{
    static char result[18] = "00:00:00:00:00:00";
    for (int i = 0; i < 6; i++) {
        result[i * 3] = "0123456789ABCDEF"[mac[i] / 16];
        result[i * 3 + 1] = "0123456789ABCDEF"[mac[i] % 16];
    }
    return result;
}
