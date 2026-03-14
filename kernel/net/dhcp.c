#include "net/dhcp.h"
#include "net/helpers.h"
#include "net/ipv4.h"
#include "net/network.h"
#include <lib/string.h>
#include <drivers/terminal.h>
#include <arpa/inet.h>

#define MAX_DNS_SERVERS 5 // Let's keep this simple

// TODO: This does not implement things that require the client to store state (like lease time)

void set_request_dhcp_options(uint8_t* options, const uint8_t ip[4], const uint8_t server_ip[4])
{
    int offset = 0;

    // Option 53: DHCP Message Type
    options[offset++] = DHCP_OPTION_MESSAGE_TYPE; // Type
    options[offset++] = DHCP_OPTION_MESSAGE_TYPE_LEN; // Length
    options[offset++] = DHCP_MESSAGE_TYPE_REQUEST;

    options[offset++] = DHCP_OPT_REQUESTED_IP_ADDR; // Option 50: Requested IP Address
    options[offset++] = 4; // Length
    options[offset++] = ip[0];
    options[offset++] = ip[1];
    options[offset++] = ip[2];
    options[offset++] = ip[3];

    options[offset++] = DHCP_OPT_SERVER_ID; // Option 54: Server Identifier
    options[offset++] = 4; // Length
    options[offset++] = server_ip[0];
    options[offset++] = server_ip[1];
    options[offset++] = server_ip[2];
    options[offset++] = server_ip[3];

    options[offset++] = DHCP_OPT_HOST_NAME; // Option 12: Host Name
    options[offset++] = 5; // Length
    options[offset++] = 'o';
    options[offset++] = 's';
    options[offset++] = 'd';
    options[offset++] = 'e';
    options[offset++] = 'v';

    // Option 255: End of Options
    options[offset] = DHCP_OPT_END;
}

int dhcp_options_get_dns_servers(const uint8_t options[static DHCP_OPTIONS_LEN], uint32_t dns_servers[static 1],
                                 size_t* dns_server_count)
{
    size_t offset = 0;
    *dns_server_count = 0;

    while (offset < DHCP_OPTIONS_LEN)
    {
        uint8_t option_code = options[offset++];

        if (option_code == DHCP_OPT_END)
        {
            break;
        }
        else if (option_code == DHCP_OPT_PAD)
        {
            continue; // Pad option, skip to the next byte
        }
        else
        {
            if (offset >= DHCP_OPTIONS_LEN)
            {
                // Malformed packet
                return -1;
            }

            uint8_t option_length = options[offset++];

            if (offset + option_length > DHCP_OPTIONS_LEN)
            {
                // Malformed packet
                return -1;
            }

            if (option_code == DHCP_OPT_DNS)
            {
                if (option_length % 4 != 0)
                {
                    // Invalid length for DNS servers (should be a multiple of 4)
                    return -1;
                }

                size_t num_servers = option_length / 4;

                for (size_t i = 0; i < num_servers && i < MAX_DNS_SERVERS; i++)
                {
                    uint32_t dns_server_network_order;
                    memcpy(&dns_server_network_order, &options[offset + i * 4], sizeof(uint32_t));
                    dns_servers[i] = dns_server_network_order;
                }

                *dns_server_count = num_servers;
                return 0; // Success
            }
            else
            {
                // Skip over the data for this option
                offset += option_length;
            }
        }
    }

    // DNS servers option not found
    return -1;
}

uint32_t dhcp_options_get_ip_option(const uint8_t options[static DHCP_OPTIONS_LEN], int option)
{
    int offset = 0;
    while (offset < DHCP_OPTIONS_LEN)
    {
        uint8_t option_code = options[offset++];

        if (option_code == DHCP_OPT_END)
        {
            break;
        }
        if (option_code == DHCP_OPT_PAD)
        {
            continue; // Pad option, skip to the next byte
        }
        // Ensure there's enough space for the length field
        if (offset >= DHCP_OPTIONS_LEN)
        {
            // Malformed packet
            return 0;
        }

        uint8_t option_length = options[offset++];

        // Ensure there's enough space for the option data
        if (offset + option_length > DHCP_OPTIONS_LEN)
        {
            // Malformed packet
            return 0;
        }

        if (option_code == option)
        {
            if (option_length != 4)
            {
                // Invalid subnet mask length
                return 0;
            }
            // Extract the subnet mask
            const uint32_t subnet_mask = ((uint32_t)options[offset] << 24) | (options[offset + 1] << 16) |
                (options[offset + 2] << 8) | options[offset + 3];
            return subnet_mask;
        }
        // Skip over the data for this option
        offset += option_length;
    }
    return 0;
}

void set_discover_dhcp_options(uint8_t* options)
{
    int offset = 0;

    // Option 53: DHCP Message Type
    options[offset++] = DHCP_OPTION_MESSAGE_TYPE; // Type
    options[offset++] = DHCP_OPTION_MESSAGE_TYPE_LEN; // Length
    options[offset++] = DHCP_MESSAGE_TYPE_DISCOVER;

    // Option 55: Parameter Request List
    options[offset++] = DHCP_OPTION_PARAMETER_REQUEST; // Type
    options[offset++] = 3; // Length (requesting 3 parameters)
    options[offset++] = DHCP_OPT_SUBNET_MASK; // Request Subnet Mask
    options[offset++] = DHCP_OPT_ROUTER; // Request Router
    options[offset++] = DHCP_OPT_DNS; // Request DNS

    // Option 255: End of Options
    options[offset] = DHCP_OPT_END;
}

void dhcp_receive(uint8_t* packet, uint16_t len)
{
    const size_t headers_len = sizeof(struct ether_header) +
                               sizeof(struct ipv4_header) +
                               sizeof(struct udp_header);
    if (len < headers_len + sizeof(struct dhcp_header))
        return;

    auto dhcp_packet = (struct dhcp_header*)(packet + headers_len);
    const uint16_t dhcp_message_type = dhcp_packet->options[2];

    // A response to our DHCP Discover (Offer)
    if (dhcp_packet->op == DHCP_OP_OFFER && dhcp_message_type == DHCP_MESSAGE_TYPE_OFFER &&
        network_get_my_mac_address() &&
        network_compare_mac_addresses(dhcp_packet->chaddr, network_get_my_mac_address()))
    {
        uint32_t dhcp_server_ip_p = dhcp_options_get_ip_option(dhcp_packet->options, DHCP_OPT_SERVER_ID);
        dhcp_server_ip_p = htonl(dhcp_server_ip_p);
        uint8_t dhcp_server_ip[4];
        memcpy(dhcp_server_ip, &dhcp_server_ip_p, 4);

        boot_message(INFO,
                     "%-13s %d.%d.%d.%d. Server: %d.%d.%d.%d",
                     "DHCP offer:",
                     dhcp_packet->yiaddr[0],
                     dhcp_packet->yiaddr[1],
                     dhcp_packet->yiaddr[2],
                     dhcp_packet->yiaddr[3],
                     dhcp_server_ip[0],
                     dhcp_server_ip[1],
                     dhcp_server_ip[2],
                     dhcp_server_ip[3]);

        dhcp_send_request(network_get_my_mac_address(), dhcp_packet->yiaddr, dhcp_packet->siaddr);
    }
    // A response to our DHCP Request (ACK)
    // This is the final step in the DHCP process, and we are now bound to the IP address
    else if (dhcp_packet->op == DHCP_OP_OFFER && dhcp_message_type == DHCP_MESSAGE_TYPE_ACK &&
        network_get_my_mac_address() &&
        network_compare_mac_addresses(dhcp_packet->chaddr, network_get_my_mac_address()))
    {
        boot_message(INFO,
                     "%-13s %d.%d.%d.%d",
                     "IP address:",
                     dhcp_packet->yiaddr[0],
                     dhcp_packet->yiaddr[1],
                     dhcp_packet->yiaddr[2],
                     dhcp_packet->yiaddr[3]);

        uint32_t subnet_mask_p = dhcp_options_get_ip_option(dhcp_packet->options, DHCP_OPT_SUBNET_MASK);
        subnet_mask_p = htonl(subnet_mask_p);
        uint8_t subnet_mask[4];
        memcpy(subnet_mask, &subnet_mask_p, 4);
        boot_message(INFO,
                     "%-13s %d.%d.%d.%d",
                     "Subnet mask:",
                     subnet_mask[0],
                     subnet_mask[1],
                     subnet_mask[2],
                     subnet_mask[3]);

        uint32_t gateway_p = dhcp_options_get_ip_option(dhcp_packet->options, DHCP_OPT_ROUTER);
        gateway_p = htonl(gateway_p);
        uint8_t gateway[4];
        memcpy(gateway, &gateway_p, 4);
        boot_message(INFO,
                     "%-13s %d.%d.%d.%d",
                     "Gateway:",
                     gateway[0],
                     gateway[1],
                     gateway[2],
                     gateway[3]);

        uint32_t dns_servers[MAX_DNS_SERVERS];
        size_t dns_server_count;
        dhcp_options_get_dns_servers(dhcp_packet->options, dns_servers, &dns_server_count);

        for (size_t i = 0; i < dns_server_count && i < MAX_DNS_SERVERS; i++)
        {
            uint32_t dns_p = dns_servers[i];
            uint8_t dns[4];
            memcpy(dns, &dns_p, 4);
            boot_message(INFO, "%-13s %d.%d.%d.%d", "DNS server:", dns[0], dns[1], dns[2], dns[3]);
        }

        network_set_my_ip_address(dhcp_packet->yiaddr);
        network_set_default_gateway(gateway);
        network_set_subnet_mask(subnet_mask);
        network_set_dns_servers(dns_servers, dns_server_count);

        network_set_state(true);
    }
}

/// @brief Shared packet builder for DHCP Discover and Request.
/// @param mac        Client MAC address (used for chaddr and, for Discover, ether src).
/// @param src_mac    MAC to place in the Ethernet source field (may differ for Request).
/// @param dhcp_op    DHCP op code (DHCP_OP_DISCOVER / DHCP_OP_REQUEST).
/// @param label      Log message printed before sending.
/// @param set_options Callback that fills dhcp_header.options; receives (options, ctx).
/// @param ctx        Opaque context forwarded to set_options.
static void dhcp_send_packet(const uint8_t mac[static 6],
                              const uint8_t src_mac[static 6],
                              uint8_t dhcp_op,
                              const char *label,
                              void (*set_options)(uint8_t *, const void *),
                              const void *ctx)
{
    boot_message(INFO, "%s", label);

    struct ether_header ether_header = {
        .dest_host = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF},
        .ether_type = htons(ETHERTYPE_IP),
    };
    memcpy(ether_header.src_host, src_mac, 6);

    constexpr uint8_t zero_ip[4] = {0, 0, 0, 0};
    constexpr uint8_t bcast_ip[4] = {0xFF, 0xFF, 0xFF, 0xFF};
    struct ipv4_header ipv4_header = {0};
    ipv4_fill_header(&ipv4_header, IP_PROTOCOL_UDP, zero_ip, bcast_ip,
                     (uint16_t)(sizeof(struct udp_header) + sizeof(struct dhcp_header)));

    struct udp_header udp_header = {
        .src_port  = htons(DHCP_SOURCE_PORT),
        .dest_port = htons(DHCP_DEST_PORT),
        .len       = htons(sizeof(struct udp_header) + sizeof(struct dhcp_header)),
        .checksum  = 0x0000,
    };

    struct dhcp_header dhcp_header = {
        .op    = dhcp_op,
        .htype = DHCP_HTYPE_ETH,
        .hlen  = DHCP_HLEN_ETH,
        .hops  = 0,
        .xid   = 0x726f626,
        .secs  = 0x0000,
        .flags = htons(DHCP_FLAG_BROADCAST),
        .ciaddr = {0},
        .yiaddr = {0},
        .siaddr = {0},
        .giaddr = {0},
        .magic = htonl(DHCP_MAGIC_COOKIE),
    };
    memcpy(dhcp_header.chaddr, mac, 6);

    set_options(dhcp_header.options, ctx);

    // Pseudo-header for UDP checksum calculation
    const struct ipv4_pseudo_header pseudo_header = {
        .src_ip  = {
            ipv4_header.source_ip[0],
            ipv4_header.source_ip[1],
            ipv4_header.source_ip[2],
            ipv4_header.source_ip[3],
        },
        .dest_ip = {
            ipv4_header.dest_ip[0],
            ipv4_header.dest_ip[1],
            ipv4_header.dest_ip[2],
            ipv4_header.dest_ip[3],
        },
        .zero       = 0,
        .protocol   = IP_PROTOCOL_UDP,
        .length = udp_header.len,
    };

    uint8_t udp_checksum_buf[sizeof(struct ipv4_pseudo_header) + sizeof(struct udp_header) + sizeof(struct dhcp_header)];
    memcpy(udp_checksum_buf, &pseudo_header, sizeof(struct ipv4_pseudo_header));
    memcpy(udp_checksum_buf + sizeof(struct ipv4_pseudo_header), &udp_header, sizeof(struct udp_header));
    memcpy(udp_checksum_buf + sizeof(struct ipv4_pseudo_header) + sizeof(struct udp_header),
           &dhcp_header, sizeof(struct dhcp_header));

    udp_header.checksum = checksum(udp_checksum_buf, sizeof(udp_checksum_buf), 0);

    const struct dhcp_packet packet = {
        .eth  = ether_header,
        .ip   = ipv4_header,
        .udp  = udp_header,
        .dhcp = dhcp_header,
    };

    network_send_packet(&packet, sizeof(struct dhcp_packet));
}

// Context struct for set_request_dhcp_options callback
struct dhcp_request_ctx {
    const uint8_t *ip;
    const uint8_t *server_ip;
};

static void set_request_options_cb(uint8_t *options, const void *ctx)
{
    const struct dhcp_request_ctx *r = ctx;
    set_request_dhcp_options(options, r->ip, r->server_ip);
}

static void set_discover_options_cb(uint8_t *options, const void *ctx)
{
    (void)ctx;
    set_discover_dhcp_options(options);
}

/// @brief Sends a DHCP Request packet.
/// This is in response to a DHCP Offer packet from the server.
void dhcp_send_request(uint8_t mac[6], uint8_t ip[4], uint8_t server_ip[4])
{
    const struct dhcp_request_ctx ctx = { .ip = ip, .server_ip = server_ip };
    dhcp_send_packet(mac, network_get_my_mac_address(), DHCP_OP_REQUEST,
                     "Sending DHCP Request...", set_request_options_cb, &ctx);
}

/// @brief Sends a DHCP Discover packet.
/// This is the first step in the DHCP process.
void dhcp_send_discover(uint8_t mac[6])
{
    dhcp_send_packet(mac, mac, DHCP_OP_DISCOVER,
                     "Sending DHCP Discover...", set_discover_options_cb, NULL);
}
