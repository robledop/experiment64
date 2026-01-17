#include <sys/socket.h>
#include <arpa/inet.h>
#include <sys/time.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <dns.h>

#define ICMP_ECHO 8
#define ICMP_REPLY 0

struct icmp_header
{
    uint8_t type;
    uint8_t code;
    uint16_t checksum;
    uint16_t id;
    uint16_t sequence;
} __attribute__((packed));

static uint16_t icmp_checksum(const void* data, size_t len)
{
    auto ptr = (const uint8_t*)data;
    uint32_t sum = 0;

    while (len > 1)
    {
        const uint16_t word = (uint16_t)ptr[0] | ((uint16_t)ptr[1] << 8);
        sum += word;
        ptr += 2;
        len -= 2;
    }

    if (len > 0)
        sum += (uint16_t)ptr[0];

    while (sum >> 16)
        sum = (sum & 0xFFFF) + (sum >> 16);

    return (uint16_t)(~sum);
}

static int send_with_retry(int sockfd, const struct sockaddr_in* dest,
                           const void* packet, size_t packet_len)
{
    for (int attempt = 0; attempt < 10; attempt++)
    {
        const ssize_t sent = sendto(sockfd, packet, packet_len, 0,
                                    (const struct sockaddr*)dest, sizeof(*dest));
        if (sent >= 0) return 0;
        usleep(10000);
    }
    return -1;
}

int main(int argc, char** argv)
{
    if (argc < 2)
    {
        printf("usage: ping <ip> [count]\n");
        return 1;
    }

    int count = 4;
    if (argc >= 3)
    {
        char* end = nullptr;
        const long parsed = strtol(argv[2], &end, 10);
        if (end == argv[2] || *end != '\0' || parsed <= 0 || parsed > 0x7fffffffL)
            count = 4;
        else
            count = (int)parsed;
    }

    uint32_t ip = inet_addr(argv[1]);
    if (ip == 0)
    {
        struct sockaddr_in resolved_address = {0};
        gethostbyname(argv[1], &resolved_address);
        bytes_to_ip(resolved_address.sin_addr, &ip);
    }

    if (ip == 0 && strcmp(argv[1], "0.0.0.0") != 0)
    {
        printf("ping: invalid address '%s'\n", argv[1]);
        return 1;
    }

    uint8_t ip_bytes[4];
    ip_to_bytes(ip, ip_bytes);

    const int sockfd = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
    if (sockfd < 0)
    {
        printf("ping: socket failed\n");
        return 1;
    }

    struct sockaddr_in dest = {0};
    dest.sin_family = AF_INET;
    dest.sin_port = 0;
    memcpy(dest.sin_addr, ip_bytes, sizeof(dest.sin_addr));

    printf("PING %d.%d.%d.%d: 32 data bytes\n",
           ip_bytes[0], ip_bytes[1], ip_bytes[2], ip_bytes[3]);

    const uint16_t id = (uint16_t)(getpid() & 0xFFFF); // Make sure the id is within 16 bits
    constexpr size_t payload_len = 32;
    uint8_t packet[sizeof(struct icmp_header) + payload_len];

    for (int seq = 1; seq <= count; seq++)
    {
        const auto hdr = (struct icmp_header*)packet;
        hdr->type = ICMP_ECHO;
        hdr->code = 0;
        hdr->checksum = 0;
        hdr->id = htons(id);
        hdr->sequence = htons((uint16_t)seq);

        // The alphabet will be used to fill the payload
        for (size_t i = 0; i < payload_len; i++)
            packet[sizeof(struct icmp_header) + i] = (uint8_t)('A' + (i % 26));

        hdr->checksum = icmp_checksum(packet, sizeof(packet));

        if (send_with_retry(sockfd, &dest, packet, sizeof(packet)) != 0)
        {
            printf("ping: send failed\n");
            return 1;
        }

        const uint64_t start = now_ms();
        const uint64_t deadline = start + 1000;
        bool got_reply = false;

        while (now_ms() < deadline)
        {
            uint8_t recvbuf[256];
            struct sockaddr_in src = {0};
            socklen_t srclen = sizeof(src);
            const ssize_t n = recvfrom(sockfd, recvbuf, sizeof(recvbuf), MSG_DONTWAIT,
                                       (struct sockaddr*)&src, &srclen);
            if (n > 0)
            {
                if ((size_t)n < sizeof(struct icmp_header))
                    continue;
                const struct icmp_header* rh = (struct icmp_header*)recvbuf;
                if (rh->type != ICMP_REPLY || rh->code != 0)
                    continue;
                if (ntohs(rh->id) != id || ntohs(rh->sequence) != (uint16_t)seq)
                    continue;

                const uint64_t rtt = now_ms() - start;
                printf("%zd bytes from %d.%d.%d.%d: icmp_seq=%d time=%llums\n",
                       n,
                       src.sin_addr[0], src.sin_addr[1], src.sin_addr[2], src.sin_addr[3],
                       seq,
                       (unsigned long long)rtt);
                got_reply = true;
                break;
            }
            usleep(10);
        }

        if (!got_reply) printf("Request timeout for icmp_seq %d\n", seq);
        if (seq != count) sleep(1000);
    }

    return 0;
}
