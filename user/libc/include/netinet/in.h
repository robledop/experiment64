#pragma once

#include <sys/socket.h>
#include <stdint.h>

struct in_addr {
    uint32_t s_addr;
};

#define INADDR_ANY ((uint32_t)0)

struct sockaddr_in {
    sa_family_t sin_family;
    uint16_t sin_port;
    struct in_addr sin_addr;
    uint8_t sin_zero[8];
};
