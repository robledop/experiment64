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

uint32_t inet_addr(const char* cp)
{
    unsigned v = 0;
    const char* start = cp;

    for (int i = 0; i < 4; i++)
    {
        int n = 0;
        while (1)
        {
            const char c = *start;
            start++;
            if (c >= '0' && c <= '9')
            {
                n *= 10;
                n += c - '0';
            }
            else if ((i < 3 && c == '.') || i == 3)
            {
                break;
            }
            else
            {
                return INVALID;
            }
        }
        if (n >= 256)
        {
            return INVALID;
        }
        v *= 256;
        v += n;
    }
    return v;
}

int inet_aton(const char *cp, struct in_addr *inp)
{
    if (!cp || !inp)
        return 0;
    uint32_t a = inet_addr(cp);
    if (a == 0 && *cp != '0')
        return 0;
    inp->s_addr = a;
    return 1;
}

static inline void write_digit(char** p, uint8_t digit)
{
    **p = (char)((unsigned char)('0' + digit));
    (*p)++;
}

static char inet_ntoa_buf[16];

char *inet_ntoa(struct in_addr in)
{
    inet_ntoa_r(in.s_addr, inet_ntoa_buf);
    return inet_ntoa_buf;
}

void inet_ntoa_r(uint32_t addr, char* buf)
{
    const uint32_t ip = ntohl(addr);
    uint8_t octets[4] = {
        (uint8_t)(ip >> 24) & 0xFF,
        (uint8_t)(ip >> 16) & 0xFF,
        (uint8_t)(ip >> 8) & 0xFF,
        (uint8_t)ip & 0xFF
    };

    char* p = buf;
    for (int i = 0; i < 4; i++)
    {
        uint8_t octet = octets[i];
        if (octet >= 100)
        {
            write_digit(&p, (uint8_t)(octet / 100));
            octet = (uint8_t)(octet % 100);
            write_digit(&p, (uint8_t)(octet / 10));
            write_digit(&p, (uint8_t)(octet % 10));
        }
        else if (octet >= 10)
        {
            write_digit(&p, (uint8_t)(octet / 10));
            write_digit(&p, (uint8_t)(octet % 10));
        }
        else
        {
            write_digit(&p, octet);
        }
        if (i < 3)
            *p++ = '.';
    }
    *p = '\0';
}

void ip_to_bytes(uint32_t ip, uint8_t out[4])
{
    out[0] = (uint8_t)((ip >> 24) & 0xFF);
    out[1] = (uint8_t)((ip >> 16) & 0xFF);
    out[2] = (uint8_t)((ip >> 8) & 0xFF);
    out[3] = (uint8_t)(ip & 0xFF);
}

void bytes_to_ip(const uint8_t bytes[4], uint32_t* out)
{
    *out = ((uint32_t)bytes[0] << 24) | ((uint32_t)bytes[1] << 16) |
        ((uint32_t)bytes[2] << 8) | (uint32_t)bytes[3];
}
