#pragma once

#include <stdint.h>

struct udp_header
{
    uint16_t src_port;
    uint16_t dest_port;
    uint16_t len;
    uint16_t checksum;
} __attribute__((packed));

struct udp_pseudo_header
{
    uint8_t src_ip[4];
    uint8_t dest_ip[4];
    uint8_t zero;        // Always 0
    uint8_t protocol;    // Protocol number (UDP is 17)
    uint16_t udp_length; // Length of UDP header + data
};
