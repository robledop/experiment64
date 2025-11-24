#include "fat32.h"
#include "heap.h"
#include "string.h"
#include "terminal.h"
#include "bio.h"
#include <stddef.h>
#include <limits.h>

static struct inode_operations fat32_iops;

typedef struct
{
    fat32_fs_t *fs;
    uint32_t dir_cluster; // Cluster of the directory containing the entry
    uint32_t dir_offset;  // Offset of the entry in that directory
} fat32_inode_data_t;

static uint32_t cluster_to_lba(fat32_fs_t *fs, uint32_t cluster)
{
    return fs->first_data_sector + ((cluster - 2) * fs->sectors_per_cluster);
}

int fat32_init(fat32_fs_t *fs, uint8_t drive_index, uint32_t partition_lba)
{
    fs->drive_index = drive_index;
    fs->partition_lba = partition_lba;

    fat32_bpb_t *bpb = kmalloc(512);
    if (!bpb)
        return 1;

    buffer_head_t *bh = bread(drive_index, partition_lba);
    if (!bh)
    {
        kfree(bpb);
        return 1;
    }
    memcpy(bpb, bh->data, 512);
    brelse(bh);

    if (bpb->boot_signature != 0x29 && bpb->boot_signature != 0x28)
    {
        // Some formatters might not set this exactly, but let's check bytes per sector
        if (bpb->bytes_per_sector != 512)
        {
            boot_message(ERROR, "FAT32: Invalid bytes per sector: %d", bpb->bytes_per_sector);
            kfree(bpb);
            return 1;
        }
    }

    fs->root_cluster = bpb->root_cluster;
    fs->sectors_per_cluster = bpb->sectors_per_cluster;
    fs->bytes_per_cluster = bpb->sectors_per_cluster * bpb->bytes_per_sector;

    uint32_t fat_size = bpb->fat_size_32;
    if (fat_size == 0)
        fat_size = bpb->fat_size_16;

    fs->fat_start_lba = partition_lba + bpb->reserved_sector_count;
    uint32_t root_dir_sectors = ((bpb->root_entry_count * 32) + (bpb->bytes_per_sector - 1)) / bpb->bytes_per_sector;

    uint32_t data_start_block = bpb->reserved_sector_count + (bpb->num_fats * fat_size) + root_dir_sectors;
    fs->first_data_sector = partition_lba + data_start_block;

    // Calculate total clusters
    // Total sectors - reserved - FATs - root dir (0 for FAT32)
    uint32_t total_sectors = bpb->total_sectors_32;
    if (total_sectors == 0)
        total_sectors = bpb->total_sectors_16;

    uint32_t data_sectors = total_sectors - data_start_block;
    fs->total_clusters = data_sectors / fs->sectors_per_cluster;

    boot_message(INFO, "FAT32 Init: Drive %d, Partition LBA %d", drive_index, partition_lba);
    boot_message(INFO, "  Root Cluster: %d", fs->root_cluster);
    boot_message(INFO, "  Sectors Per Cluster: %d", fs->sectors_per_cluster);
    boot_message(INFO, "  First Data Sector: %d", fs->first_data_sector);

    kfree(bpb);
    return 0;
}

static int fat32_read_cluster(fat32_fs_t *fs, uint32_t cluster, uint8_t *buffer)
{
    uint32_t lba = cluster_to_lba(fs, cluster);
    for (uint32_t i = 0; i < fs->sectors_per_cluster; i++)
    {
        buffer_head_t *bh = bread(fs->drive_index, lba + i);
        if (!bh)
            return 1;
        memcpy(buffer + i * 512, bh->data, 512);
        brelse(bh);
    }
    return 0;
}

static int fat32_write_cluster(fat32_fs_t *fs, uint32_t cluster, uint8_t *buffer)
{
    uint32_t lba = cluster_to_lba(fs, cluster);
    for (uint32_t i = 0; i < fs->sectors_per_cluster; i++)
    {
        buffer_head_t *bh = bread(fs->drive_index, lba + i);
        if (!bh)
            return 1;
        memcpy(bh->data, buffer + i * 512, 512);
        bwrite(bh);
        brelse(bh);
    }
    return 0;
}

static int fat32_read_fat_entry(fat32_fs_t *fs, uint32_t cluster, uint32_t *value)
{
    uint32_t fat_offset = cluster * 4;
    uint32_t fat_sector = fs->fat_start_lba + (fat_offset / 512);
    uint32_t ent_offset = fat_offset % 512;

    buffer_head_t *bh = bread(fs->drive_index, fat_sector);
    if (!bh)
        return 1;

    *value = *(uint32_t *)&bh->data[ent_offset] & 0x0FFFFFFF;
    brelse(bh);
    return 0;
}

static int fat32_write_fat_entry(fat32_fs_t *fs, uint32_t cluster, uint32_t value)
{
    uint32_t fat_offset = cluster * 4;
    uint32_t fat_sector = fs->fat_start_lba + (fat_offset / 512);
    uint32_t ent_offset = fat_offset % 512;

    buffer_head_t *bh = bread(fs->drive_index, fat_sector);
    if (!bh)
        return 1;

    *(uint32_t *)&bh->data[ent_offset] = value;
    bwrite(bh);
    brelse(bh);
    return 0;
}

static uint32_t fat32_find_free_cluster(fat32_fs_t *fs)
{
    // Simple scan from cluster 2
    // Limit to total clusters + 2 (since clusters start at 2)
    uint32_t max_cluster = fs->total_clusters + 2;

    // printk("Scanning for free cluster up to %d\n", max_cluster);

    for (uint32_t i = 2; i < max_cluster; i++)
    {
        uint32_t entry;
        if (fat32_read_fat_entry(fs, i, &entry) != 0)
        {
            printk("fat32_find_free_cluster: Read error at cluster %d\n", i);
            break;
        }
        if (entry == 0)
            return i;
    }
    return 0;
}

// Helper to convert "FILENAMEEXT" (11 chars) to "filename.ext"
static void fat_name_to_str(const char *fat_name, char *out_name)
{
    int i, j;
    // Copy name
    for (i = 0; i < 8; i++)
    {
        if (fat_name[i] == ' ')
            break;
        out_name[i] = fat_name[i];
    }

    // Check extension
    if (fat_name[8] != ' ')
    {
        out_name[i++] = '.';
        for (j = 0; j < 3; j++)
        {
            if (fat_name[8 + j] == ' ')
                break;
            out_name[i++] = fat_name[8 + j];
        }
    }
    out_name[i] = 0;
}

// Helper to compare "filename.ext" with "FILENAMEEXT"
static int fat_name_cmp(const char *filename, const char *fat_name)
{
    char temp[13];
    fat_name_to_str((char *)fat_name, temp);

    const char *s1 = filename;
    const char *s2 = temp;
    while (*s1 && *s2)
    {
        char c1 = *s1;
        char c2 = *s2;
        if (c1 >= 'a' && c1 <= 'z')
            c1 -= 32;
        if (c2 >= 'a' && c2 <= 'z')
            c2 -= 32;
        if (c1 != c2)
            return 1;
        s1++;
        s2++;
    }
    return *s1 - *s2;
}

static int fat32_find_entry(fat32_fs_t *fs, uint32_t dir_cluster, const char *name, fat32_directory_entry_t *output_entry, uint32_t *found_cluster, uint32_t *found_offset)
{
    uint32_t current_cluster = dir_cluster;
    uint8_t *cluster_buf = kmalloc(fs->bytes_per_cluster);
    if (!cluster_buf)
        return 1;

    while (current_cluster < 0x0FFFFFF8 && current_cluster != 0)
    {
        if (fat32_read_cluster(fs, current_cluster, cluster_buf) != 0)
        {
            kfree(cluster_buf);
            return 1;
        }

        fat32_directory_entry_t *entry = (fat32_directory_entry_t *)cluster_buf;
        size_t entries_per_cluster = fs->bytes_per_cluster / sizeof(fat32_directory_entry_t);

        for (size_t i = 0; i < entries_per_cluster; i++)
        {
            if (entry[i].name[0] == 0x00)
            {
                kfree(cluster_buf);
                return 1; // End of dir
            }
            if (entry[i].name[0] == 0xE5)
                continue;
            if (entry[i].attr & ATTR_LONG_NAME)
                continue;

            if (fat_name_cmp(name, (char *)entry[i].name) == 0)
            {
                if (output_entry)
                    memcpy(output_entry, &entry[i], sizeof(fat32_directory_entry_t));
                if (found_cluster)
                    *found_cluster = current_cluster;
                if (found_offset)
                    *found_offset = i * sizeof(fat32_directory_entry_t);
                kfree(cluster_buf);
                return 0;
            }
        }

        // Next cluster in chain
        uint32_t next_cluster;
        if (fat32_read_fat_entry(fs, current_cluster, &next_cluster) != 0)
            break;
        current_cluster = next_cluster;
    }

    kfree(cluster_buf);
    return 1;
}

static int fat32_resolve_parent(fat32_fs_t *fs, const char *path, uint32_t *parent_cluster, char *filename)
{
    uint32_t current_cluster = fs->root_cluster;
    const char *p = path;
    if (*p == '/')
        p++;

    while (*p)
    {
        char component[13];
        int i = 0;
        while (*p && *p != '/' && i < 12)
            component[i++] = *p++;
        component[i] = 0;
        while (*p == '/')
            p++;

        if (*p == 0)
        {
            // Last component
            strcpy(filename, component);
            *parent_cluster = current_cluster;
            return 0;
        }

        // Not last, must be dir
        fat32_directory_entry_t entry;
        if (fat32_find_entry(fs, current_cluster, component, &entry, NULL, NULL) != 0)
            return 1;
        if (!(entry.attr & ATTR_DIRECTORY))
            return 1;

        current_cluster = (entry.fst_clus_hi << 16) | entry.fst_clus_lo;
        if (current_cluster == 0)
            current_cluster = fs->root_cluster;
    }
    return 1; // Empty path
}

void fat32_list_dir(fat32_fs_t *fs, const char *path)
{
    uint32_t dir_cluster;

    if (path == NULL || path[0] == 0 || (path[0] == '/' && path[1] == 0))
    {
        dir_cluster = fs->root_cluster;
    }
    else
    {
        fat32_file_info_t info;
        if (fat32_stat(fs, path, &info) != 0)
        {
            printk("Directory not found: %s\n", path);
            return;
        }

        if (!(info.attributes & ATTR_DIRECTORY))
        {
            printk("Not a directory: %s\n", path);
            return;
        }

        dir_cluster = info.first_cluster;
    }

    uint8_t *cluster_buf = kmalloc(fs->bytes_per_cluster);
    if (!cluster_buf)
        return;

    uint32_t current_cluster = dir_cluster;

    printk("Directory Listing of %s:\n", path ? path : "/");

    while (1)
    {
        if (fat32_read_cluster(fs, current_cluster, cluster_buf) != 0)
            break;

        fat32_directory_entry_t *entry = (fat32_directory_entry_t *)cluster_buf;
        size_t entries_per_cluster = fs->bytes_per_cluster / sizeof(fat32_directory_entry_t);

        for (size_t i = 0; i < entries_per_cluster; i++)
        {
            if (entry[i].name[0] == 0x00)
                goto end; // End of directory
            if (entry[i].name[0] == 0xE5)
                continue; // Deleted
            if (entry[i].attr & ATTR_LONG_NAME)
                continue; // Skip LFN

            char name[13];
            fat_name_to_str((char *)entry[i].name, name);

            printk("  %s", name);
            if (entry[i].attr & ATTR_DIRECTORY)
                printk("/");
            printk(" (%d bytes)\n", entry[i].file_size);
        }

        // Next cluster
        uint32_t next;
        if (fat32_read_fat_entry(fs, current_cluster, &next) != 0)
            break;

        if (next >= 0x0FFFFFF8)
            break;

        current_cluster = next;
    }

end:
    kfree(cluster_buf);
}

int fat32_stat(fat32_fs_t *fs, const char *path, fat32_file_info_t *info)
{
    uint32_t parent_cluster;
    char filename[13];

    if (fat32_resolve_parent(fs, path, &parent_cluster, filename) != 0)
        return 1;

    fat32_directory_entry_t entry;
    if (fat32_find_entry(fs, parent_cluster, filename, &entry, NULL, NULL) != 0)
        return 1;

    fat_name_to_str((char *)entry.name, info->name);
    info->size = entry.file_size;
    info->attributes = entry.attr;
    info->first_cluster = (entry.fst_clus_hi << 16) | entry.fst_clus_lo;
    if (info->first_cluster == 0)
        info->first_cluster = fs->root_cluster;
    info->inode = info->first_cluster;

    return 0;
}

// VFS Integration

static uint64_t fat32_vfs_read(vfs_inode_t *node, uint64_t offset, uint64_t size, uint8_t *buffer)
{
    fat32_inode_data_t *data = (fat32_inode_data_t *)node->device;
    fat32_fs_t *fs = data->fs;
    uint32_t current_cluster = node->inode;
    uint32_t bytes_per_cluster = fs->bytes_per_cluster;

    if (offset >= node->size)
        return 0;
    if (offset + size > node->size)
        size = node->size - offset;

    // Skip clusters
    uint32_t clusters_to_skip = offset / bytes_per_cluster;
    uint32_t cluster_offset = offset % bytes_per_cluster;

    for (uint32_t i = 0; i < clusters_to_skip; i++)
    {
        uint32_t next_cluster;
        if (fat32_read_fat_entry(fs, current_cluster, &next_cluster) != 0)
            return 0;
        if (next_cluster >= 0x0FFFFFF8)
            return 0;
        current_cluster = next_cluster;
    }

    uint64_t bytes_read = 0;
    uint8_t *cluster_buf = kmalloc(bytes_per_cluster);
    if (!cluster_buf)
        return 0;

    while (bytes_read < size)
    {
        if (fat32_read_cluster(fs, current_cluster, cluster_buf) != 0)
            break;

        uint32_t chunk_size = bytes_per_cluster - cluster_offset;
        if (chunk_size > size - bytes_read)
            chunk_size = size - bytes_read;

        memcpy(buffer + bytes_read, cluster_buf + cluster_offset, chunk_size);
        bytes_read += chunk_size;
        cluster_offset = 0;

        if (bytes_read < size)
        {
            uint32_t next_cluster;
            if (fat32_read_fat_entry(fs, current_cluster, &next_cluster) != 0)
                break;
            if (next_cluster >= 0x0FFFFFF8)
                break;
            current_cluster = next_cluster;
        }
    }

    kfree(cluster_buf);
    return bytes_read;
}

static uint64_t fat32_vfs_write(vfs_inode_t *node, uint64_t offset, uint64_t size, uint8_t *buffer)
{
    fat32_inode_data_t *data = (fat32_inode_data_t *)node->device;
    fat32_fs_t *fs = data->fs;
    uint32_t current_cluster = node->inode;
    uint32_t bytes_per_cluster = fs->bytes_per_cluster;

    // Seek to offset
    uint32_t clusters_to_skip = offset / bytes_per_cluster;
    uint32_t cluster_offset = offset % bytes_per_cluster;

    for (uint32_t i = 0; i < clusters_to_skip; i++)
    {
        uint32_t next_cluster;
        if (fat32_read_fat_entry(fs, current_cluster, &next_cluster) != 0)
            return 0; // Error

        if (next_cluster >= 0x0FFFFFF8)
        {
            // End of chain, but we need to write further. Allocate new cluster.
            uint32_t new_cluster = fat32_find_free_cluster(fs);
            if (new_cluster == 0)
                return 0; // Disk full

            fat32_write_fat_entry(fs, current_cluster, new_cluster);
            fat32_write_fat_entry(fs, new_cluster, 0x0FFFFFFF);

            // Clear new cluster
            uint8_t *zero_buf = kmalloc(bytes_per_cluster);
            if (zero_buf)
            {
                memset(zero_buf, 0, bytes_per_cluster);
                fat32_write_cluster(fs, new_cluster, zero_buf);
                kfree(zero_buf);
            }

            current_cluster = new_cluster;
        }
        else
        {
            current_cluster = next_cluster;
        }
    }

    uint64_t bytes_written = 0;

    while (bytes_written < size)
    {
        uint32_t chunk_size = bytes_per_cluster - cluster_offset;
        if (chunk_size > size - bytes_written)
            chunk_size = size - bytes_written;

        // Read-modify-write if partial cluster
        uint8_t *cluster_buf = kmalloc(bytes_per_cluster);
        if (!cluster_buf)
            break;

        if (fat32_read_cluster(fs, current_cluster, cluster_buf) != 0)
        {
            kfree(cluster_buf);
            break;
        }

        memcpy(cluster_buf + cluster_offset, buffer + bytes_written, chunk_size);

        if (fat32_write_cluster(fs, current_cluster, cluster_buf) != 0)
        {
            kfree(cluster_buf);
            break;
        }
        kfree(cluster_buf);

        bytes_written += chunk_size;
        cluster_offset = 0;

        if (bytes_written < size)
        {
            // Need next cluster
            uint32_t next_cluster;
            if (fat32_read_fat_entry(fs, current_cluster, &next_cluster) != 0)
                break;

            if (next_cluster >= 0x0FFFFFF8)
            {
                // Allocate new
                uint32_t new_cluster = fat32_find_free_cluster(fs);
                if (new_cluster == 0)
                    break;

                fat32_write_fat_entry(fs, current_cluster, new_cluster);
                fat32_write_fat_entry(fs, new_cluster, 0x0FFFFFFF);

                // Clear new cluster
                uint8_t *zero_buf = kmalloc(bytes_per_cluster);
                if (zero_buf)
                {
                    memset(zero_buf, 0, bytes_per_cluster);
                    fat32_write_cluster(fs, new_cluster, zero_buf);
                    kfree(zero_buf);
                }

                current_cluster = new_cluster;
            }
            else
            {
                current_cluster = next_cluster;
            }
        }
    }

    // Update file size if we extended it
    if (offset + bytes_written > node->size)
    {
        node->size = offset + bytes_written;

        // Update directory entry
        if (data->dir_cluster != 0)
        {
            uint8_t *dir_buf = kmalloc(bytes_per_cluster);
            if (dir_buf)
            {
                if (fat32_read_cluster(fs, data->dir_cluster, dir_buf) == 0)
                {
            fat32_directory_entry_t *entries = (fat32_directory_entry_t *)dir_buf;
            size_t idx = data->dir_offset / sizeof(fat32_directory_entry_t);
            if (idx < fs->bytes_per_cluster / sizeof(fat32_directory_entry_t))
            {
                entries[idx].file_size = node->size;
                fat32_write_cluster(fs, data->dir_cluster, dir_buf);
            }
        }
        kfree(dir_buf);
    }
        }
    }

    return bytes_written;
}

static void fat32_vfs_open([[maybe_unused]] vfs_inode_t *node) {}
static void fat32_vfs_close(vfs_inode_t *node)
{
    if (node->device)
    {
        kfree(node->device);
        node->device = NULL;
    }
}

static vfs_dirent_t *fat32_vfs_readdir(vfs_inode_t *node, uint32_t index)
{
    fat32_inode_data_t *data = (fat32_inode_data_t *)node->device;
    fat32_fs_t *fs = data->fs;
    uint32_t current_cluster = node->inode;
    uint8_t *cluster_buf = kmalloc(fs->bytes_per_cluster);
    if (!cluster_buf)
        return NULL;

    uint32_t count = 0;
    vfs_dirent_t *dirent = NULL;

    while (1)
    {
        if (fat32_read_cluster(fs, current_cluster, cluster_buf) != 0)
            break;

        fat32_directory_entry_t *entry = (fat32_directory_entry_t *)cluster_buf;
        size_t entries_per_cluster = fs->bytes_per_cluster / sizeof(fat32_directory_entry_t);

        for (size_t i = 0; i < entries_per_cluster; i++)
        {
            if (entry[i].name[0] == 0x00)
                goto end;
            if (entry[i].name[0] == 0xE5)
                continue;
            if (entry[i].attr & ATTR_LONG_NAME)
                continue;

            if (count == index)
            {
                dirent = kmalloc(sizeof(vfs_dirent_t));
                fat_name_to_str((char *)entry[i].name, dirent->name);
                dirent->inode = (entry[i].fst_clus_hi << 16) | entry[i].fst_clus_lo;
                if (dirent->inode == 0)
                    dirent->inode = fs->root_cluster;
                goto end;
            }
            count++;
        }

        uint32_t next;
        if (fat32_read_fat_entry(fs, current_cluster, &next) != 0)
            break;
        if (next >= 0x0FFFFFF8)
            break;
        current_cluster = next;
    }

end:
    kfree(cluster_buf);
    return dirent;
}

static vfs_inode_t *fat32_vfs_finddir(const vfs_inode_t *node, const char *name)
{
    fat32_inode_data_t *data = (fat32_inode_data_t *)node->device;
    fat32_fs_t *fs = data->fs;
    uint32_t dir_cluster = node->inode;

    fat32_directory_entry_t entry;
    uint32_t found_cluster;
    uint32_t found_offset;
    if (fat32_find_entry(fs, dir_cluster, name, &entry, &found_cluster, &found_offset) != 0)
        return nullptr;

    vfs_inode_t *new_node = kmalloc(sizeof(vfs_inode_t));
    memset(new_node, 0, sizeof(vfs_inode_t));

    new_node->inode = (entry.fst_clus_hi << 16) | entry.fst_clus_lo;
    if (new_node->inode == 0)
        new_node->inode = fs->root_cluster;

    new_node->size = entry.file_size;
    new_node->flags = (entry.attr & ATTR_DIRECTORY) ? VFS_DIRECTORY : VFS_FILE;

    fat32_inode_data_t *new_data = kmalloc(sizeof(fat32_inode_data_t));
    new_data->fs = fs;
    new_data->dir_cluster = found_cluster;
    new_data->dir_offset = found_offset;
    new_node->device = new_data;

    new_node->iops = &fat32_iops;

    return new_node;
}

static vfs_inode_t *fat32_vfs_clone(vfs_inode_t *node)
{
    vfs_inode_t *new_node = kmalloc(sizeof(vfs_inode_t));
    memcpy(new_node, node, sizeof(vfs_inode_t));

    fat32_inode_data_t *old_data = (fat32_inode_data_t *)node->device;
    fat32_inode_data_t *new_data = kmalloc(sizeof(fat32_inode_data_t));
    memcpy(new_data, old_data, sizeof(fat32_inode_data_t));
    new_node->device = new_data;

    return new_node;
}

static struct inode_operations fat32_iops = {
    .read = fat32_vfs_read,
    .write = fat32_vfs_write,
    .open = fat32_vfs_open,
    .close = fat32_vfs_close,
    .readdir = fat32_vfs_readdir,
    .finddir = fat32_vfs_finddir,
    .clone = fat32_vfs_clone,
};

vfs_inode_t *fat32_mount(uint8_t drive_index, uint32_t partition_lba)
{
    fat32_fs_t *fs = kmalloc(sizeof(fat32_fs_t));
    if (fat32_init(fs, drive_index, partition_lba) != 0)
    {
        kfree(fs);
        return nullptr;
    }

    vfs_inode_t *root = kmalloc(sizeof(vfs_inode_t));
    memset(root, 0, sizeof(vfs_inode_t));
    root->flags = VFS_DIRECTORY;
    root->inode = fs->root_cluster;

    fat32_inode_data_t *data = kmalloc(sizeof(fat32_inode_data_t));
    data->fs = fs;
    data->dir_cluster = 0;
    data->dir_offset = 0;
    root->device = data;

    root->iops = &fat32_iops;

    return root;
}

int fat32_read_file(fat32_fs_t *fs, const char *filename, uint8_t *buffer, uint32_t buffer_size)
{
    fat32_file_info_t info;
    if (fat32_stat(fs, filename, &info) != 0)
        return 2; // Not found

    if (info.attributes & ATTR_DIRECTORY)
        return 4; // Is a directory

    vfs_inode_t temp_node;
    memset(&temp_node, 0, sizeof(vfs_inode_t));

    fat32_inode_data_t data;
    data.fs = fs;
    data.dir_cluster = 0;
    data.dir_offset = 0;
    temp_node.device = &data;

    temp_node.inode = info.first_cluster;
    temp_node.size = info.size;

    uint64_t read = fat32_vfs_read(&temp_node, 0, buffer_size, buffer);
    if (read == 0 && info.size > 0 && buffer_size > 0)
        return 6;

    return 0;
}

static void str_to_fat_name(const char *filename, char *fat_name)
{
    memset(fat_name, ' ', 11);
    int i = 0;
    int j = 0;

    // Name
    while (filename[i] && filename[i] != '.' && j < 8)
    {
        char c = filename[i++];
        if (c >= 'a' && c <= 'z')
            c -= 32; // To upper
        fat_name[j++] = c;
    }

    // Skip to dot
    while (filename[i] && filename[i] != '.')
        i++;

    if (filename[i] == '.')
    {
        i++;
        j = 8;
        while (filename[i] && j < 11)
        {
            char c = filename[i++];
            if (c >= 'a' && c <= 'z')
                c -= 32;
            fat_name[j++] = c;
        }
    }
}

static int fat32_add_entry(fat32_fs_t *fs, uint32_t dir_cluster, const char *name, uint8_t attr, uint32_t first_cluster, uint32_t size)
{
    uint32_t current_cluster = dir_cluster;
    uint8_t *cluster_buf = kmalloc(fs->bytes_per_cluster);
    if (!cluster_buf)
        return 1;

    while (1)
    {
        if (fat32_read_cluster(fs, current_cluster, cluster_buf) != 0)
        {
            kfree(cluster_buf);
            return 1;
        }

        fat32_directory_entry_t *entry = (fat32_directory_entry_t *)cluster_buf;
        size_t entries_per_cluster = fs->bytes_per_cluster / sizeof(fat32_directory_entry_t);
        size_t free_idx = SIZE_MAX;

        for (size_t i = 0; i < entries_per_cluster; i++)
        {
            if (entry[i].name[0] == 0x00 || entry[i].name[0] == 0xE5)
            {
                free_idx = i;
                break;
            }
        }

        if (free_idx != SIZE_MAX)
        {
            // Found free slot
            memset(&entry[free_idx], 0, sizeof(fat32_directory_entry_t));
            str_to_fat_name(name, (char *)entry[free_idx].name);
            entry[free_idx].attr = attr;
            entry[free_idx].fst_clus_hi = (first_cluster >> 16) & 0xFFFF;
            entry[free_idx].fst_clus_lo = first_cluster & 0xFFFF;
            entry[free_idx].file_size = size;

            fat32_write_cluster(fs, current_cluster, cluster_buf);
            kfree(cluster_buf);
            return 0;
        }

        // No free slot in this cluster. Check next.
        uint32_t next_cluster;
        if (fat32_read_fat_entry(fs, current_cluster, &next_cluster) != 0)
            break;

        if (next_cluster >= 0x0FFFFFF8)
        {
            // End of chain, allocate new cluster for directory
            uint32_t new_cluster = fat32_find_free_cluster(fs);
            if (new_cluster == 0)
                break;

            fat32_write_fat_entry(fs, current_cluster, new_cluster);
            fat32_write_fat_entry(fs, new_cluster, 0x0FFFFFFF);

            // Clear new cluster
            memset(cluster_buf, 0, fs->bytes_per_cluster);
            fat32_write_cluster(fs, new_cluster, cluster_buf);

            current_cluster = new_cluster;
            // Loop again, will find slot 0 in new cluster
        }
        else
        {
            current_cluster = next_cluster;
        }
    }

    kfree(cluster_buf);
    return 1;
}

int fat32_create_file(fat32_fs_t *fs, const char *path)
{
    uint32_t parent_cluster;
    char filename[13];

    if (fat32_resolve_parent(fs, path, &parent_cluster, filename) != 0)
        return 1;

    fat32_directory_entry_t existing;
    if (fat32_find_entry(fs, parent_cluster, filename, &existing, nullptr, NULL) == 0)
        return 1; // Exists

    // Allocate first cluster for file
    uint32_t cluster = fat32_find_free_cluster(fs);
    if (cluster == 0)
        return 1;
    fat32_write_fat_entry(fs, cluster, 0x0FFFFFFF); // EOC

    // Clear the new file cluster
    uint8_t *cluster_buf = kmalloc(fs->bytes_per_cluster);
    if (!cluster_buf)
        return 1;
    memset(cluster_buf, 0, fs->bytes_per_cluster);
    fat32_write_cluster(fs, cluster, cluster_buf);
    kfree(cluster_buf);

    // Add entry to parent
    return fat32_add_entry(fs, parent_cluster, filename, ATTR_ARCHIVE, cluster, 0);
}

int fat32_create_dir(fat32_fs_t *fs, const char *path)
{
    uint32_t parent_cluster;
    char dirname[13];

    if (fat32_resolve_parent(fs, path, &parent_cluster, dirname) != 0)
        return 1;

    fat32_directory_entry_t existing;
    if (fat32_find_entry(fs, parent_cluster, dirname, &existing, nullptr, NULL) == 0)
        return 1; // Exists

    // Allocate cluster for new dir
    uint32_t cluster = fat32_find_free_cluster(fs);
    if (cluster == 0)
        return 1;
    fat32_write_fat_entry(fs, cluster, 0x0FFFFFFF); // EOC

    // Initialize new directory with . and ..
    uint8_t *cluster_buf = kmalloc(fs->bytes_per_cluster);
    if (!cluster_buf)
        return 1;
    memset(cluster_buf, 0, fs->bytes_per_cluster);

    fat32_directory_entry_t *entry = (fat32_directory_entry_t *)cluster_buf;

    // . entry
    memset(&entry[0], 0, sizeof(fat32_directory_entry_t));
    memset(entry[0].name, ' ', 11);
    entry[0].name[0] = '.';
    entry[0].attr = ATTR_DIRECTORY;
    entry[0].fst_clus_hi = (cluster >> 16) & 0xFFFF;
    entry[0].fst_clus_lo = cluster & 0xFFFF;

    // .. entry
    memset(&entry[1], 0, sizeof(fat32_directory_entry_t));
    memset(entry[1].name, ' ', 11);
    entry[1].name[0] = '.';
    entry[1].name[1] = '.';
    entry[1].attr = ATTR_DIRECTORY;
    // Parent cluster. If parent is root, it should be 0.
    uint32_t parent_link = (parent_cluster == fs->root_cluster) ? 0 : parent_cluster;
    entry[1].fst_clus_hi = (parent_link >> 16) & 0xFFFF;
    entry[1].fst_clus_lo = parent_link & 0xFFFF;

    fat32_write_cluster(fs, cluster, cluster_buf);
    kfree(cluster_buf);

    // Add entry to parent
    return fat32_add_entry(fs, parent_cluster, dirname, ATTR_DIRECTORY, cluster, 0);
}

int fat32_write_file(fat32_fs_t *fs, const char *path, uint8_t *buffer, uint32_t size)
{
    fat32_file_info_t info;
    if (fat32_stat(fs, path, &info) != 0)
    {
        // Try to create
        if (fat32_create_file(fs, path) != 0)
            return 1;
        if (fat32_stat(fs, path, &info) != 0)
            return 1;
    }

    uint32_t current_cluster = info.first_cluster;
    uint32_t bytes_written = 0;

    while (bytes_written < size)
    {
        uint32_t chunk = size - bytes_written;
        if (chunk > fs->bytes_per_cluster)
            chunk = fs->bytes_per_cluster;

        uint8_t *temp_buf = kmalloc(fs->bytes_per_cluster);
        if (!temp_buf)
            return 1;

        memset(temp_buf, 0, fs->bytes_per_cluster);
        memcpy(temp_buf, buffer + bytes_written, chunk);

        fat32_write_cluster(fs, current_cluster, temp_buf);
        kfree(temp_buf);

        bytes_written += chunk;

        if (bytes_written < size)
        {
            // Need next cluster
            uint32_t next;
            if (fat32_read_fat_entry(fs, current_cluster, &next) != 0)
                return 1;

            if (next >= 0x0FFFFFF8)
            {
                // Allocate new
                uint32_t new_cluster = fat32_find_free_cluster(fs);
                if (new_cluster == 0)
                    return 1;

                fat32_write_fat_entry(fs, current_cluster, new_cluster);
                fat32_write_fat_entry(fs, new_cluster, 0x0FFFFFFF);
                current_cluster = new_cluster;
            }
            else
            {
                current_cluster = next;
            }
        }
    }

    // Update file size in directory
    uint32_t parent_cluster;
    char filename[13];
    if (fat32_resolve_parent(fs, path, &parent_cluster, filename) != 0)
        return 1;

    fat32_directory_entry_t entry;
    uint32_t dir_cluster_num;
    uint32_t dir_offset;

    if (fat32_find_entry(fs, parent_cluster, filename, &entry, &dir_cluster_num, &dir_offset) == 0)
    {
        // Found it, update size
        uint8_t *cluster_buf = kmalloc(fs->bytes_per_cluster);
        if (!cluster_buf)
            return 1;

        if (fat32_read_cluster(fs, dir_cluster_num, cluster_buf) == 0)
        {
            fat32_directory_entry_t *entries = (fat32_directory_entry_t *)cluster_buf;
            size_t idx = dir_offset / sizeof(fat32_directory_entry_t);
            if (idx < fs->bytes_per_cluster / sizeof(fat32_directory_entry_t))
            {
                entries[idx].file_size = size;
                fat32_write_cluster(fs, dir_cluster_num, cluster_buf);
            }
        }
        kfree(cluster_buf);
    }

    return 0;
}

int fat32_delete_file(fat32_fs_t *fs, const char *path)
{
    uint32_t parent_cluster;
    char filename[13];
    if (fat32_resolve_parent(fs, path, &parent_cluster, filename) != 0)
    {
        return 1;
    }

    fat32_directory_entry_t entry;
    uint32_t dir_cluster_num;
    uint32_t dir_offset;

    if (fat32_find_entry(fs, parent_cluster, filename, &entry, &dir_cluster_num, &dir_offset) == 0)
    {
        // Found it
        uint32_t cluster = (entry.fst_clus_hi << 16) | entry.fst_clus_lo;
        uint32_t freed = 0;

        // Mark deleted in directory
        uint8_t *cluster_buf = kmalloc(fs->bytes_per_cluster);
        if (!cluster_buf)
            return 1;

        if (fat32_read_cluster(fs, dir_cluster_num, cluster_buf) == 0)
        {
            fat32_directory_entry_t *entries = (fat32_directory_entry_t *)cluster_buf;
            size_t idx = dir_offset / sizeof(fat32_directory_entry_t);
            if (idx < fs->bytes_per_cluster / sizeof(fat32_directory_entry_t))
            {
                entries[idx].name[0] = 0xE5; // Deleted marker
                fat32_write_cluster(fs, dir_cluster_num, cluster_buf);
            }
        }
        kfree(cluster_buf);

        // Free chain
        while (cluster < 0x0FFFFFF8 && cluster != 0)
        {
            if (cluster < 2 || cluster >= fs->total_clusters + 2)
            {
                printk("fat32_delete_file: Invalid cluster %d (Total: %d)\n", cluster, fs->total_clusters);
                break;
            }

            uint32_t next;
            if (fat32_read_fat_entry(fs, cluster, &next) != 0)
            {
                printk("fat32_delete_file: Failed to read FAT entry for cluster %d\n", cluster);
                break;
            }
            if (fat32_write_fat_entry(fs, cluster, 0) != 0)
            {
                printk("fat32_delete_file: Failed to write FAT entry for cluster %d\n", cluster);
                break;
            }
            cluster = next;
            freed++;
        }
        if (freed == 0 && cluster == 0)
            printk("fat32_delete_file: freed 0 clusters for %s\n", filename);
        return 0;
    }
    return 1; // Not found
}
