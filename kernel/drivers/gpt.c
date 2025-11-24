#include "gpt.h"
#include "bio.h"
#include "heap.h"
#include "string.h"
#include "terminal.h"

#define GPT_SIGNATURE 0x5452415020494645ULL

static const uint8_t EFI_SYSTEM_PARTITION_GUID[16] = {
    0x28, 0x73, 0x2A, 0xC1, 0x1F, 0xF8, 0xD2, 0x11,
    0xBA, 0x4B, 0x00, 0xA0, 0xC9, 0x3E, 0xC9, 0x3B};

static const uint8_t MICROSOFT_BASIC_DATA_GUID[16] = {
    0xA2, 0xA0, 0xD0, 0xEB, 0xE5, 0xB9, 0x33, 0x44,
    0x87, 0xC0, 0x68, 0xB6, 0xB7, 0x26, 0x99, 0xC7};

static const uint8_t LINUX_FILESYSTEM_GUID[16] = {
    0xAF, 0x3D, 0xC6, 0x0F, 0x83, 0x84, 0x72, 0x47,
    0x8E, 0x79, 0x3D, 0x69, 0xD8, 0x47, 0x7D, 0xE4};

const char *gpt_get_guid_name(uint8_t *type_guid)
{
    if (memcmp(type_guid, EFI_SYSTEM_PARTITION_GUID, 16) == 0)
        return "EFI System Partition";
    if (memcmp(type_guid, MICROSOFT_BASIC_DATA_GUID, 16) == 0)
        return "Microsoft Basic Data";
    if (memcmp(type_guid, LINUX_FILESYSTEM_GUID, 16) == 0)
        return "Linux Filesystem";
    return "Unknown";
}

void gpt_read_partitions(uint8_t drive, partition_callback_t callback)
{
    // Read GPT Header (LBA 1)
    buffer_head_t *bh = bread(drive, 1);
    if (!bh)
    {
        printk("GPT: Failed to read LBA 1 on drive %d\n", drive);
        return;
    }

    gpt_header_t *header = (gpt_header_t *)bh->data;

    if (header->signature != GPT_SIGNATURE)
    {
        printk("GPT: Invalid signature on drive %d\n", drive);
        brelse(bh);
        return;
    }

    uint32_t num_entries = header->num_partition_entries;
    uint32_t entry_size = header->size_partition_entry;
    uint64_t entries_lba = header->partition_entries_lba;

    // printk("GPT: Detected valid GPT on drive %d\n", drive);
    // printk("GPT: %d entries starting at LBA %lu\n", num_entries, entries_lba);

    brelse(bh);

    // Read Partition Entries
    // Calculate how many sectors we need to read
    uint32_t total_size = num_entries * entry_size;
    uint32_t sectors = (total_size + 511) / 512;

    uint8_t *entries_buf = kmalloc(sectors * 512);
    if (!entries_buf)
    {
        printk("GPT: Failed to allocate memory for entries\n");
        return;
    }

    for (uint32_t i = 0; i < sectors; i++)
    {
        bh = bread(drive, entries_lba + i);
        if (!bh)
        {
            printk("GPT: Failed to read partition entries sector %lu\n", entries_lba + i);
            kfree(entries_buf);
            return;
        }
        memcpy(entries_buf + i * 512, bh->data, 512);
        brelse(bh);
    }

    for (uint32_t i = 0; i < num_entries; i++)
    {
        gpt_entry_t *entry = (gpt_entry_t *)(entries_buf + i * entry_size);

        // Check if entry is used (Type GUID is not zero)
        int empty = 1;
        for (int j = 0; j < 16; j++)
        {
            if (entry->type_guid[j] != 0)
            {
                empty = 0;
                break;
            }
        }

        if (empty)
            continue;

        partition_info_t info;
        info.drive = drive;
        info.start_lba = entry->first_lba;
        info.end_lba = entry->last_lba;
        memcpy(info.type_guid, entry->type_guid, 16);

        // Convert name to ASCII
        for (int j = 0; j < 36; j++)
        {
            info.name[j] = (char)entry->name[j];
            if (info.name[j] == 0)
                break;
        }
        info.name[36] = 0;

        if (callback)
        {
            callback(&info);
        }
    }

    kfree(entries_buf);
}
