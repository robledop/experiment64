#include "net/helpers.h"
#include "string.h"

// https://www.lemoda.net/c/ip-to-integer/

#define INVALID 0

/// @brief Convert the character string in "ip" into an unsigned integer.
/// This assumes that an unsigned integer contains at least 32 bits.
unsigned int ip_to_int(const char ip[static 1])
{
    unsigned v = 0;
    const char *start = ip;

    for (int i = 0; i < 4; i++) {
        int n = 0;
        while (1) {
            const char c = *start;
            start++;
            if (c >= '0' && c <= '9') {
                n *= 10;
                n += c - '0';
            } else if ((i < 3 && c == '.') || i == 3) {
                break;
            } else {
                return INVALID;
            }
        }
        if (n >= 256) {
            return INVALID;
        }
        v *= 256;
        v += n;
    }
    return v;
}

uint32_t ntohl(uint32_t netlong)
{
    return ((netlong & 0x000000ff) << 24) | ((netlong & 0x0000ff00) << 8) | ((netlong & 0x00ff0000) >> 8) |
           ((netlong & 0xff000000) >> 24);
}

void int_to_ip(uint32_t ip_addr, char *result[16])
{
    const uint32_t ip = ntohl(ip_addr);

    uint8_t ip_address[4];

    ip_address[0] = (ip >> 24) & 0xFF;
    ip_address[1] = (ip >> 16) & 0xFF;
    ip_address[2] = (ip >> 8) & 0xFF;
    ip_address[3] = ip & 0xFF;

    for (uint32_t i = 0; i < 4; i++) {
        char buffer[4];
        snprintk(buffer, sizeof(buffer), "%u", ip_address[i]);
        if (i < 3) {
            strcat(buffer, ".");
        }
        strcat(*result, buffer);
    }
}

/// @brief Computes the checksum according to the algorithm described in
/// <a href="https://datatracker.ietf.org/doc/html/rfc1071#section-4.1">RFC 1071</a>.
/// @code
/// checksum(header, header->ihl * 4, 0);
/// @endcode
uint16_t checksum(void *addr, int count, const int start_sum)
{
    uint32_t sum = start_sum;
    const uint8_t *ptr = (const uint8_t *)addr;

    // Process 16-bit words (read as bytes to avoid alignment issues)
    // Little-endian: first byte is low, second byte is high
    while (count > 1) {
        uint16_t word = (uint16_t)ptr[0] | ((uint16_t)ptr[1] << 8);
        sum += word;
        ptr += 2;
        count -= 2;
    }

    // Handle odd byte
    if (count > 0) {
        sum += (uint16_t)ptr[0];
    }

    // Fold 32-bit sum to 16 bits
    while (sum >> 16)
        sum = (sum & 0xffff) + (sum >> 16);

    return ~sum;
}

/// @brief Converts the unsigned short integer netshort from network byte order to host byte order.
uint16_t ntohs(const uint16_t data)
{
    return ((data & 0x00ff) << 8) | (data & 0xff00) >> 8;
}

/// @brief Converts the unsigned short integer hostshort from host byte order to network byte order.
uint16_t htons(const uint16_t data)
{
    return ((data & 0x00ff) << 8) | (data & 0xff00) >> 8;
}

/// @brief Converts the unsigned long integer netlong from network byte order to host byte order.
uint32_t htonl(const uint32_t data)
{
    return ((data & 0x000000ff) << 24) | ((data & 0x0000ff00) << 8) | ((data & 0x00ff0000) >> 8) |
           ((data & 0xff000000) >> 24);
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
