#include "vfs.h"
#include "string.h"
#include "terminal.h"
#include "fat32.h"
#include "ext2.h"
#include "gpt.h"
#include <stdbool.h>

vfs_inode_t *vfs_root = 0;

void vfs_init()
{
    // Initialize root as null or a temporary placeholder if needed
    vfs_root = 0;
}

static partition_info_t root_part;
static partition_info_t mnt_part;
static bool root_found = false;
static bool mnt_found = false;

static void mount_callback(partition_info_t *part)
{
    const char *type = gpt_get_guid_name(part->type_guid);
    // Check for Microsoft Basic Data (FAT32) or EFI System Partition
    if (strcmp(type, "Microsoft Basic Data") == 0)
    {
        mnt_part = *part;
        mnt_found = true;
        boot_message(INFO, "VFS: Found Data partition at LBA %ld", part->start_lba);
    }
    else if (strcmp(type, "Linux Filesystem") == 0)
    {
        root_part = *part;
        root_found = true;
        boot_message(INFO, "VFS: Found Root partition at LBA %ld", part->start_lba);
    }
}

void vfs_mount_root(void)
{
    // Try to find partition via GPT on Drive 0
    gpt_read_partitions(0, mount_callback);

    if (root_found)
    {
        vfs_root = ext2_mount(root_part.drive, root_part.start_lba);
        if (vfs_root)
        {
            boot_message(INFO, "VFS: Mounted EXT2 on /");
        }
        else
        {
            boot_message(ERROR, "VFS: Failed to mount EXT2 on /");
        }
    }

    if (!vfs_root)
    {
        boot_message(WARNING, "VFS: GPT mount failed or no root found, trying fallback LBA 2048");
        // Fallback
        vfs_root = fat32_mount(0, 2048);
        if (vfs_root)
        {
            boot_message(INFO, "VFS: Mounted FAT32 on / (Fallback)");
        }
        else
        {
            boot_message(ERROR, "VFS: Failed to mount FAT32");
        }
    }

    if (vfs_root && mnt_found)
    {
        vfs_inode_t *mnt_node = vfs_finddir(vfs_root, "mnt");
        if (mnt_node)
        {
            vfs_inode_t *fat_root = fat32_mount(mnt_part.drive, mnt_part.start_lba);
            if (fat_root)
            {
                mnt_node->ptr = fat_root;
                mnt_node->flags |= VFS_MOUNTPOINT;
                boot_message(INFO, "VFS: Mounted FAT32 on /mnt");
            }
            else
            {
                boot_message(ERROR, "VFS: Failed to mount FAT32 on /mnt");
            }
        }
        else
        {
            boot_message(WARNING, "VFS: /mnt not found in root, skipping FAT32 mount");
        }
    }
}

uint64_t vfs_read(vfs_inode_t *node, uint64_t offset, uint64_t size, uint8_t *buffer)
{
    if (node->iops && node->iops->read)
        return node->iops->read(node, offset, size, buffer);
    return 0;
}

uint64_t vfs_write(vfs_inode_t *node, uint64_t offset, uint64_t size, uint8_t *buffer)
{
    if (node->iops && node->iops->write)
        return node->iops->write(node, offset, size, buffer);
    return 0;
}

void vfs_open(vfs_inode_t *node)
{
    if (node->iops && node->iops->open)
        node->iops->open(node);
}

void vfs_close(vfs_inode_t *node)
{
    if (node->iops && node->iops->close)
        node->iops->close(node);
}

vfs_dirent_t *vfs_readdir(vfs_inode_t *node, uint32_t index)
{
    if ((node->flags & 0x07) == VFS_DIRECTORY && node->iops && node->iops->readdir)
        return node->iops->readdir(node, index);
    return 0;
}

vfs_inode_t *vfs_finddir(vfs_inode_t *node, char *name)
{
    if ((node->flags & 0x07) == VFS_DIRECTORY && node->iops && node->iops->finddir)
    {
        vfs_inode_t *child = node->iops->finddir(node, name);
        if (child && (child->flags & VFS_MOUNTPOINT) && child->ptr)
        {
            return (vfs_inode_t *)child->ptr;
        }
        return child;
    }
    return 0;
}

vfs_inode_t *vfs_resolve_path(const char *path)
{
    if (!path || !vfs_root)
        return 0;

    vfs_inode_t *current = vfs_root;

    // Handle absolute paths (treat as relative to root for now)
    while (*path == '/')
        path++;

    if (*path == 0)
        return current; // Root

    char name[128]; // Max filename length
    int name_idx = 0;

    while (*path)
    {
        if (*path == '/')
        {
            name[name_idx] = 0;
            if (name_idx > 0)
            {
                vfs_inode_t *next = vfs_finddir(current, name);
                if (!next)
                    return 0;
                current = next;
            }
            name_idx = 0;
        }
        else
        {
            if (name_idx < 127)
                name[name_idx++] = *path;
        }
        path++;
    }

    // Last component
    if (name_idx > 0)
    {
        name[name_idx] = 0;
        vfs_inode_t *next = vfs_finddir(current, name);
        if (!next)
            return 0;
        current = next;
    }

    return current;
}
