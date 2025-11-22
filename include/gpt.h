#pragma once

#include <stdint.h>

typedef struct
{
    uint64_t signature;
    uint32_t revision;
    uint32_t header_size;
    uint32_t header_crc32;
    uint32_t reserved;
    uint64_t current_lba;
    uint64_t backup_lba;
    uint64_t first_usable_lba;
    uint64_t last_usable_lba;
    uint8_t disk_guid[16];
    uint64_t partition_entries_lba;
    uint32_t num_partition_entries;
    uint32_t size_partition_entry;
    uint32_t partition_entries_crc32;
} __attribute__((packed)) gpt_header_t;

typedef struct
{
    uint8_t type_guid[16];
    uint8_t unique_guid[16];
    uint64_t first_lba;
    uint64_t last_lba;
    uint64_t attributes;
    uint16_t name[36]; // UTF-16LE
} __attribute__((packed)) gpt_entry_t;

typedef struct
{
    uint8_t drive;
    uint64_t start_lba;
    uint64_t end_lba;
    uint8_t type_guid[16];
    char name[37]; // ASCII
} partition_info_t;

// Callback function type for partition enumeration
typedef void (*partition_callback_t)(partition_info_t *part);

void gpt_read_partitions(uint8_t drive, partition_callback_t callback);
const char *gpt_get_guid_name(uint8_t *type_guid);
