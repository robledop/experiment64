#pragma once

#include <stdint.h>
#include <stddef.h>
#include <lib/list.h>
#include <task/spinlock.h>

// Domain
#define PF_INET 1 // IPv4
#define AF_INET PF_INET

// Type
#define SOCK_STREAM 1
#define SOCK_DGRAM 2
#define SOCK_RAW 3

// Protocol
#define IPPROTO_TCP 6
#define IPPROTO_UDP 17
#define IPPROTO_ICMP 1

#define MSG_DONTWAIT 0x01

#define SOCKET_RX_MAX_PACKETS 16

#define SOCKET_FLAG_TCP_SYN_RCVD 0x00000001u
#define SOCKET_FLAG_TCP_ESTABLISHED 0x00000002u

typedef enum
{
    SOCKET_STATE_UNBOUND = 0,
    SOCKET_STATE_BOUND,
    SOCKET_STATE_CONNECTED,
    SOCKET_STATE_LISTENING,
    SOCKET_STATE_CLOSED,
} socket_state_t;

typedef struct
{
    uint8_t ip[4];
    uint16_t port;
} socket_addr_t;

typedef struct socket_rx_packet
{
    list_item_t list;
    socket_addr_t from;
    size_t len;
    uint8_t* data;
} socket_rx_packet_t;

typedef uint16_t sa_family_t;
typedef uint32_t socklen_t;

struct sockaddr
{
    sa_family_t sa_family;
    char sa_data[14];
};
typedef struct sockaddr sockaddr_t;

struct sockaddr_in
{
    sa_family_t sin_family;
    uint16_t sin_port;
    uint8_t sin_addr[4];
    uint8_t sin_zero[8];
};
typedef struct sockaddr_in sockaddr_in_t;

typedef struct
{
    int domain;
    int type;
    int protocol;
    socket_state_t state;
    socket_addr_t local;
    socket_addr_t remote;
    uint32_t flags;
    uint32_t ref;
    uint32_t tcp_send_next;
    uint32_t tcp_recv_next;
    list_item_t accept_queue;
    list_item_t accept_list;
    size_t accept_queue_len;
    int backlog;
    spinlock_t accept_lock;
    list_item_t list;
    list_item_t rx_queue;
    size_t rx_queue_len;
    spinlock_t rx_lock;
} socket_t;

void socket_register(socket_t* sock);
void socket_unregister(socket_t* sock);
int socket_assign_port(socket_t* sock, const uint8_t ip[static 4], uint16_t requested_port, uint16_t* out_port);
socket_t* socket_find_tcp_listener(const uint8_t dest_ip[static 4], uint16_t dest_port);
socket_t* socket_find_tcp_connected(const uint8_t dest_ip[static 4], uint16_t dest_port,
                                    const uint8_t src_ip[static 4], uint16_t src_port);
int socket_deliver_udp(const uint8_t dest_ip[static 4], uint16_t dest_port,
                       const uint8_t src_ip[static 4], uint16_t src_port,
                       const uint8_t* payload, size_t payload_len);
int socket_deliver_tcp(const uint8_t dest_ip[static 4], uint16_t dest_port,
                       const uint8_t src_ip[static 4], uint16_t src_port, const uint8_t* payload, size_t payload_len);
int socket_deliver_icmp(const uint8_t dest_ip[static 4], const uint8_t src_ip[static 4],
                        const uint8_t* payload, size_t payload_len);
socket_rx_packet_t* socket_rx_pop(socket_t* sock, bool block);
