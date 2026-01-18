#pragma once

#include <stddef.h>
#include <stdint.h>
#include <unistd.h>

// Domain
#define PF_INET 1
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

typedef uint16_t sa_family_t;
typedef uint32_t socklen_t;

struct sockaddr
{
    sa_family_t sa_family;
    char sa_data[14];
};

struct sockaddr_in
{
    sa_family_t sin_family;
    uint16_t sin_port;
    uint8_t sin_addr[4];
    uint8_t sin_zero[8];
};

int socket(int domain, int type, int protocol);
int bind(int sockfd, const struct sockaddr *addr, size_t addrlen);
int listen(int sockfd, int backlog);
int accept(int sockfd, struct sockaddr *addr, socklen_t addrlen);
ssize_t sendto(int sockfd, const void *buf, size_t len, int flags,
               const struct sockaddr *dest_addr, socklen_t addrlen);
ssize_t recvfrom(int sockfd, void *buf, size_t len, int flags,
                 struct sockaddr *src_addr, socklen_t *addrlen);
