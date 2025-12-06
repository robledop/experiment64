#pragma once

#include <stdint.h>

uint16_t checksum(void *addr, int count, int start_sum);

unsigned int ip_to_int(const char ip[static 1]);
void int_to_ip(uint32_t ip_addr, char *result[static 16]);
uint16_t ntohs(uint16_t data);
uint16_t htons(uint16_t data);
uint32_t htonl(uint32_t data);
char *get_mac_address_string(uint8_t mac[static 6]);
