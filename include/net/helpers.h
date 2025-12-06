#pragma once

#include <stdint.h>

// Kernel-specific network utilities

// Computes checksum per RFC 1071 (used for IP, ICMP, UDP, TCP)
uint16_t checksum(void *addr, int count, int start_sum);

// Format MAC address as "XX:XX:XX:XX:XX:XX" string
// Returns pointer to static buffer (not thread-safe)
char *get_mac_address_string(uint8_t mac[static 6]);
