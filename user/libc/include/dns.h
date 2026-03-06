#pragma once

#include <stdint.h>

#define HEADER_SIZE 12

// ###############################################
// # QR | OPCODE | AA | TC | RD | RA | Z | RCODE |
// # 1  | 4      | 1  | 1  | 1  | 1  | 3 | 4     |
// ###############################################

#define DNS_FLAG_QR 0x8000 // Query/Response
#define DNS_FLAG_OPCODE 0x7800 // Operation code
#define DNS_FLAG_AA 0x0400 // Authoritative answer
#define DNS_FLAG_TC 0x0200 // Truncated
#define DNS_FLAG_RD 0x0100 // Recursion desired
#define DNS_FLAG_RA 0x0080 // Recursion available
#define DNS_FLAG_Z 0x0070 // Reserved
#define DNS_FLAG_RCODE 0x000f // Response code

#define DNS_QR_QUERY 0
#define DNS_QR_RESPONSE 1

#define DNS_CLASS_IN 1
#define DNS_TYPE_A 1
#define DNS_TYPE_NS 2
#define DNS_TYPE_CNAME 5
#define DNS_TYPE_SOA 6
#define DNS_TYPE_PTR 12
#define DNS_TYPE_MX 15
#define DNS_TYPE_TXT 16
#define DNS_TYPE_AAAA 28
#define DNS_TYPE_SRV 33

#define DNS_OPCODE_QUERY 0
#define DNS_OPCODE_IQUERY 1
#define DNS_OPCODE_STATUS 2
#define DNS_OPCODE_NOTIFY 4
#define DNS_OPCODE_UPDATE 5

#define DNS_RCODE_NO_ERROR 0
#define DNS_RCODE_FORMAT_ERROR 1
#define DNS_RCODE_SERVER_FAILURE 2
#define DNS_RCODE_NAME_ERROR 3
#define DNS_RCODE_NOT_IMPLEMENTED 4
#define DNS_RCODE_REFUSED 5
#define DNS_RCODE_YXDOMAIN 6
#define DNS_RCODE_YXRRSET 7
#define DNS_RCODE_NXRRSET 8
#define DNS_RCODE_NOTAUTH 9
#define DNS_RCODE_NOTZONE 10

struct dns_header
{
    uint16_t id;      // Packet ID
    uint16_t flags;   // Flags/Opcode (network order)
    uint16_t qdcount; // Question count
    uint16_t ancount; // Answer count
    uint16_t nscount; // Authority record count
    uint16_t arcount; // Additional record count
} __attribute__((packed));

static_assert(sizeof(struct dns_header) == HEADER_SIZE, "dns header size mismatch");

struct dns_question
{
    char qname[256];
    uint16_t qtype;
    uint16_t qclass;
};

struct dns_record
{
    char name[256];
    uint16_t type;
    uint16_t class;
    uint32_t ttl;
    uint16_t rdlength;
    uint8_t rdata[4];
};

struct dns_message
{
    struct dns_header header;
    struct dns_question *questions;
    struct dns_record *answers;
    struct dns_record *authorities;
    struct dns_record *additionals;
};

struct q_name
{
    char name[256];
    int qname_length;
    int offset;
};

struct dns_pool_item
{
    struct sockaddr_in *address;
    struct dns_message *message;
    struct dns_pool_item *next;
};

uint32_t dns_lookup(const char *name, struct sockaddr_in *address);