#pragma once

#include <stdint.h>
#include <stddef.h>
#include <net/socket.h>

#define TCP_FLAG_FIN 0x01 // Finish
#define TCP_FLAG_SYN 0x02 // Synchronize
#define TCP_FLAG_RST 0x04 // Reset
#define TCP_FLAG_PSH 0x08 // Push
#define TCP_FLAG_ACK 0x10 // Acknowledge
#define TCP_FLAG_URG 0x20 // Urgent
#define TCP_FLAG_ECE 0x40 // ECN-Echo
#define TCP_FLAG_CWR 0x80 // Congestion Window Reduced

struct tcp_header
{
    uint16_t src_port;
    uint16_t dst_port;
    uint32_t seq_num;
    uint32_t ack_num;

    /* data\_offset: high 4 bits, reserved: low 4 bits */
    uint8_t data_offset_reserved;

    /* flags: CWR|ECE|URG|ACK|PSH|RST|SYN|FIN */
    uint8_t flags;

    uint16_t window;
    uint16_t checksum;
    uint16_t urgent_ptr;
    /* options follow if data\_offset \> 5 */
    // ...
} __attribute__((packed));

void tcp_receive(uint8_t* packet, uint16_t len, size_t ip_len, size_t ip_header_len);
int tcp_send_segment(socket_t* sock, const uint8_t dest_ip[static 4], uint16_t dest_port,
                     uint32_t seq_num, uint32_t ack_num, uint8_t flags,
                     const uint8_t* payload, size_t payload_len, const uint8_t* dest_mac);
