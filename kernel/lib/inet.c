#include <arpa/inet.h>

uint32_t ntohl(uint32_t netlong)
{
    return ((netlong & 0x000000ff) << 24) | ((netlong & 0x0000ff00) << 8) |
           ((netlong & 0x00ff0000) >> 8) | ((netlong & 0xff000000) >> 24);
}

uint16_t ntohs(const uint16_t netshort)
{
    return ((netshort & 0x00ff) << 8) | ((netshort & 0xff00) >> 8);
}

uint16_t htons(const uint16_t hostshort)
{
    return ((hostshort & 0x00ff) << 8) | ((hostshort & 0xff00) >> 8);
}

uint32_t htonl(const uint32_t hostlong)
{
    return ((hostlong & 0x000000ff) << 24) | ((hostlong & 0x0000ff00) << 8) |
           ((hostlong & 0x00ff0000) >> 8) | ((hostlong & 0xff000000) >> 24);
}

#define INVALID 0

uint32_t inet_addr(const char *cp)
{
    unsigned v = 0;
    const char *start = cp;

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

void inet_ntoa_r(uint32_t addr, char *buf)
{
    const uint32_t ip = ntohl(addr);
    uint8_t octets[4] = {
        (ip >> 24) & 0xFF,
        (ip >> 16) & 0xFF,
        (ip >> 8) & 0xFF,
        ip & 0xFF
    };

    char *p = buf;
    for (int i = 0; i < 4; i++) {
        uint8_t octet = octets[i];
        if (octet >= 100) {
            *p++ = '0' + octet / 100;
            octet %= 100;
            *p++ = '0' + octet / 10;
            *p++ = '0' + octet % 10;
        } else if (octet >= 10) {
            *p++ = '0' + octet / 10;
            *p++ = '0' + octet % 10;
        } else {
            *p++ = '0' + octet;
        }
        if (i < 3)
            *p++ = '.';
    }
    *p = '\0';
}

