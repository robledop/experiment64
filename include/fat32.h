#pragma once

#include <stdint.h>

typedef struct __attribute__((packed))
{
    uint8_t jump_boot[3];
    uint8_t oem_name[8];
    uint16_t bytes_per_sector;
    uint8_t sectors_per_cluster;
    uint16_t reserved_sector_count;
    uint8_t num_fats;
    uint16_t root_entry_count;
    uint16_t total_sectors_16;
    uint8_t media;
    uint16_t fat_size_16;
    uint16_t sectors_per_track;
    uint16_t num_heads;
    uint32_t hidden_sectors;
    uint32_t total_sectors_32;
    uint32_t fat_size_32;
    uint16_t ext_flags;
    uint16_t fs_version;
    uint32_t root_cluster;
    uint16_t fs_info;
    uint16_t backup_boot_sector;
    uint8_t reserved[12];
    uint8_t drive_number;
    uint8_t reserved1;
    uint8_t boot_signature;
    uint32_t volume_id;
    uint8_t volume_label[11];
    uint8_t fs_type[8];
} fat32_bpb_t;

typedef struct __attribute__((packed))
{
    uint8_t name[11];
    uint8_t attr;
    uint8_t nt_res;
    uint8_t crt_time_tenth;
    uint16_t crt_time;
    uint16_t crt_date;
    uint16_t lst_acc_date;
    uint16_t fst_clus_hi;
    uint16_t wrt_time;
    uint16_t wrt_date;
    uint16_t fst_clus_lo;
    uint32_t file_size;
} fat32_directory_entry_t;

#define ATTR_READ_ONLY 0x01
#define ATTR_HIDDEN 0x02
#define ATTR_SYSTEM 0x04
#define ATTR_VOLUME_ID 0x08
#define ATTR_DIRECTORY 0x10
#define ATTR_ARCHIVE 0x20
#define ATTR_LONG_NAME 0x0F

typedef struct
{
    uint8_t drive_index;
    uint32_t partition_lba;
    uint32_t root_cluster;
    uint32_t first_data_sector;
    uint32_t sectors_per_cluster;
    uint32_t bytes_per_cluster;
    uint32_t fat_start_lba;
    uint32_t total_clusters;
} fat32_fs_t;

typedef struct
{
    char name[13];
    uint32_t size;
    uint64_t inode;
    uint8_t attributes;
    uint32_t first_cluster;
} fat32_file_info_t;

#include "vfs.h"

int fat32_init(fat32_fs_t *fs, uint8_t drive_index, uint32_t partition_lba);
vfs_inode_t *fat32_mount(uint8_t drive_index, uint32_t partition_lba);
int fat32_read_file(fat32_fs_t *fs, const char *filename, uint8_t *buffer, uint32_t buffer_size);
void fat32_list_dir(fat32_fs_t *fs, const char *path);
int fat32_stat(fat32_fs_t *fs, const char *filename, fat32_file_info_t *info);
int fat32_create_file(fat32_fs_t *fs, const char *filename);
int fat32_create_dir(fat32_fs_t *fs, const char *path);
int fat32_write_file(fat32_fs_t *fs, const char *filename, uint8_t *buffer, uint32_t size);
int fat32_delete_file(fat32_fs_t *fs, const char *filename);
