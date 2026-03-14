#pragma once

#include <stdint.h>
#include <stddef.h>
#include <net/socket.h>

struct udp_header
{
    uint16_t src_port;
    uint16_t dest_port;
    uint16_t len;
    uint16_t checksum;
} __attribute__((packed));

void udp_receive(uint8_t* packet, uint16_t len, size_t ip_len, size_t ip_header_len);
int udp_sendto(const void* buf, size_t len, socket_t* sock, struct sockaddr_in in, const uint8_t* my_ip,
               uint8_t src_ip[static 4]);
