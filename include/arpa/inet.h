#pragma once

#include <stdint.h>

// Network byte order conversion functions (POSIX)
uint16_t ntohs(uint16_t netshort);
uint16_t htons(uint16_t hostshort);
uint32_t ntohl(uint32_t netlong);
uint32_t htonl(uint32_t hostlong);

// IP address conversion functions
// inet_addr: Convert IPv4 dotted-decimal string to network byte order integer
// Returns 0 (INADDR_ANY) on invalid input
uint32_t inet_addr(const char *cp);

// inet_ntoa: Convert network byte order integer to dotted-decimal string
// Writes result to provided buffer (must be at least 16 bytes)
void inet_ntoa_r(uint32_t addr, char *buf);

