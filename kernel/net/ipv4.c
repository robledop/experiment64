#include <net/ipv4.h>
#include <net/helpers.h>
#include <lib/string.h>
#include <arpa/inet.h>

void ipv4_fill_header(struct ipv4_header *ip, const uint8_t protocol,
                      const uint8_t src[4], const uint8_t dst[4],
                      const uint16_t payload_len)
{
    ip->version = 4;
    ip->ihl = 5;
    ip->dscp_ecn = 0;
    ip->total_length = htons(sizeof(struct ipv4_header) + payload_len);
    ip->identification = 0;
    ip->flags_fragment_offset = 0;
    ip->ttl = 64;
    ip->protocol = protocol;
    ip->header_checksum = 0;
    memcpy(ip->source_ip, src, 4);
    memcpy(ip->dest_ip, dst, 4);
    ip->header_checksum = checksum(ip, (int)sizeof(struct ipv4_header), 0);
}
