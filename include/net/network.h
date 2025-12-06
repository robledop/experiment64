#pragma once

#include <stdint.h>
#include <stdbool.h>

void network_receive(uint8_t *packet, uint16_t len);
int network_send_packet(const void *data, uint16_t len);
void network_set_mac(const uint8_t mac_addr[static 6]);
uint8_t *network_get_my_ip_address(void);
bool network_compare_ip_addresses(const uint8_t ip1[static 4], const uint8_t ip2[static 4]);
bool network_compare_mac_addresses(const uint8_t mac1[static 6], const uint8_t mac2[static 6]);
uint8_t *network_get_my_mac_address(void);
bool network_is_ready(void);
void network_set_dns_servers(uint32_t dns_servers_p[static 1], uint32_t dns_server_count);
void network_set_my_ip_address(const uint8_t ip[static 4]);
void network_set_subnet_mask(const uint8_t ip[static 4]);
void network_set_default_gateway(const uint8_t ip[static 4]);
void network_set_state(bool state);
