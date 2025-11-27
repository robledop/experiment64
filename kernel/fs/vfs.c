#include "vfs.h"
#include "string.h"
#include "terminal.h"
#include "fat32.h"
#include "ext2.h"
#include <stddef.h>
#include "gpt.h"
#include <stdbool.h>
#include "heap.h"

vfs_inode_t *vfs_root = nullptr;

struct mount_point
{
    char name[64];
    vfs_inode_t *root;
};

static struct mount_point mount_table[16];
static int mount_count = 0;

void vfs_register_mount(const char *name, vfs_inode_t *root)
{
    if (mount_count < 16)
    {
        strncpy(mount_table[mount_count].name, name, 63);
        mount_table[mount_count].root = root;
        mount_count++;
    }
}

vfs_inode_t *vfs_check_mount(const char *name)
{
    for (int i = 0; i < mount_count; i++)
    {
        if (strcmp(mount_table[i].name, name) == 0)
        {
            vfs_inode_t *root = mount_table[i].root;
            if (root && root->iops && root->iops->clone)
            {
                return root->iops->clone(root);
            }

            vfs_inode_t *copy = kmalloc(sizeof(vfs_inode_t));
            if (copy)
                memcpy(copy, mount_table[i].root, sizeof(vfs_inode_t));
            return copy;
        }
    }
    return nullptr;
}

void vfs_init()
{
    vfs_root = nullptr;
}

static partition_info_t root_part;
static partition_info_t mnt_part;
static partition_info_t disk1_part;
static bool root_found = false;
static bool mnt_found = false;
static bool disk1_found = false;

static void mount_disk1_callback(partition_info_t *part)
{
    const char *type = gpt_get_guid_name(part->type_guid);
    if (strcmp(type, "Linux Filesystem") == 0)
    {
        disk1_part = *part;
        disk1_found = true;
        boot_message(INFO, "VFS: Found disk1 ext2 partition at LBA %ld", part->start_lba);
    }
}

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
            kfree(mnt_node); // Free the EXT2 node, we will mount over it
            vfs_inode_t *fat_root = fat32_mount(mnt_part.drive, mnt_part.start_lba);
            if (fat_root)
            {
                vfs_register_mount("mnt", fat_root);
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

    // Mount disk1 (IDE ext2) at /disk1 if present on drive 1
    disk1_found = false;
    gpt_read_partitions(1, mount_disk1_callback);

    if (vfs_root && disk1_found)
    {
        vfs_inode_t *node = vfs_finddir(vfs_root, "disk1");
        if (node)
        {
            kfree(node); // replace placeholder
            vfs_inode_t *ext_root = ext2_mount(disk1_part.drive, disk1_part.start_lba);
            if (ext_root)
            {
                vfs_register_mount("disk1", ext_root);
                boot_message(INFO, "VFS: Mounted EXT2 on /disk1");
            }
            else
            {
                boot_message(ERROR, "VFS: Failed to mount EXT2 on /disk1");
            }
        }
        else
        {
            boot_message(WARNING, "VFS: /disk1 not found in root, skipping disk1 mount");
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

int vfs_truncate(vfs_inode_t *node)
{
    if (node->iops && node->iops->truncate)
        return node->iops->truncate(node);
    return -1;
}

int vfs_ioctl(vfs_inode_t *node, int request, void *arg)
{
    if (node->iops && node->iops->ioctl)
        return node->iops->ioctl(node, request, arg);
    return -1;
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
    {
        // 1. Try to get entry from underlying filesystem
        vfs_dirent_t *dirent = node->iops->readdir(node, index);

        if (dirent || node != vfs_root)
        {
            return dirent;
        }

        // 2. If underlying FS is done (dirent == nullptr) AND we are at root,
        // check for virtual mount points that are NOT on disk.

        // Count real entries to determine offset
        uint32_t real_count = 0;
        if (index > 0)
        {
            while (1)
            {
                vfs_dirent_t *d = node->iops->readdir(node, real_count);
                if (!d)
                    break;
                kfree(d);
                real_count++;
            }
        }

        if (index < real_count)
            return nullptr; // Should have been caught by step 1

        uint32_t virt_index = index - real_count;
        uint32_t current_virt = 0;

        for (int i = 0; i < mount_count; i++)
        {
            // Check if this mount point exists on disk
            bool on_disk = false;
            if (node->iops->finddir)
            {
                vfs_inode_t *found = node->iops->finddir(node, mount_table[i].name);
                if (found)
                {
                    on_disk = true;
                    kfree(found);
                }
            }

            if (!on_disk)
            {
                if (current_virt == virt_index)
                {
                    vfs_dirent_t *virt_ent = kmalloc(sizeof(vfs_dirent_t));
                    if (!virt_ent)
                        return nullptr;
                    strncpy(virt_ent->name, mount_table[i].name, 127);
                    virt_ent->name[127] = '\0';
                    virt_ent->inode = 0;
                    return virt_ent;
                }
                current_virt++;
            }
        }

        return nullptr;
    }
    return nullptr;
}

vfs_inode_t *vfs_finddir(vfs_inode_t *node, char *name)
{
    if ((node->flags & 0x07) == VFS_DIRECTORY && node->iops && node->iops->finddir)
    {
        // Check mounts if we are at root
        if (node == vfs_root)
        {
            vfs_inode_t *mounted = vfs_check_mount(name);
            if (mounted)
            {
                return mounted;
            }
        }

        vfs_inode_t *child = node->iops->finddir(node, name);
        return child;
    }
    return nullptr;
}

vfs_inode_t *vfs_resolve_path(const char *path)
{
    if (!path || !vfs_root)
        return nullptr;

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
                    return nullptr;
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
            return nullptr;
        current = next;
    }

    return current;
}

int vfs_mknod(char *path, int mode, int dev)
{
    if (!path || !vfs_root)
        return -1;

    char parent_path[VFS_MAX_PATH];
    char filename[128];
    char *last_slash = strrchr(path, '/');

    if (last_slash)
    {
        ptrdiff_t len = last_slash - path;
        if (len <= 0)
        {
            strcpy(parent_path, "/");
        }
        else
        {
            if ((size_t)len >= VFS_MAX_PATH)
                return -1;
            strncpy(parent_path, path, (size_t)len);
            parent_path[len] = 0;
        }
        if (strlen(last_slash + 1) >= 128)
            return -1;
        strcpy(filename, last_slash + 1);
    }
    else
    {
        strcpy(parent_path, "/");
        if (strlen(path) >= 128)
            return -1;
        strcpy(filename, path);
    }

    vfs_inode_t *parent = vfs_resolve_path(parent_path);
    if (!parent)
    {
        printk("vfs_mknod: failed to resolve parent path '%s'\n", parent_path);
        return -1;
    }

    if ((parent->flags & VFS_DIRECTORY) && parent->iops && parent->iops->mknod)
    {
        return parent->iops->mknod(parent, filename, mode, dev);
    }

    return -1;
}
