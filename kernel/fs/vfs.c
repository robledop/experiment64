#include <fs/vfs.h>
#include <lib/string.h>
#include <drivers/terminal.h>
#include <fs/fat32.h>
#include <fs/ext2.h>
#include <stddef.h>
#include <drivers/gpt.h>
#include <io/storage.h>
#include <mem/heap.h>
#include <lib/path.h>
#include <status.h>

vfs_inode_t *vfs_root = nullptr;

struct mount_point
{
    char name[64];     // Mount name used for path matching.
    vfs_inode_t *root; // Root inode for the mounted filesystem.
};

static struct mount_point mount_table[16];
static int mount_count = 0;

void vfs_register_mount(const char *name, vfs_inode_t *root)
{
    if (mount_count < 16) {
        strncpy(mount_table[mount_count].name, name, 63);
        mount_table[mount_count].name[63] = '\0';
        mount_table[mount_count].root     = root;
        mount_count++;
    }
}

vfs_inode_t *vfs_check_mount(const char *name)
{
    for (int i = 0; i < mount_count; i++) {
        if (strcmp(mount_table[i].name, name) == 0) {
            vfs_inode_t *root = mount_table[i].root;
            if (root && root->iops && root->iops->clone) {
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

static void vfs_put_inode(vfs_inode_t *node)
{
    if (!node || node == vfs_root)
        return;
    vfs_close(node);
    kfree(node);
}

struct gpt_scan_state
{
    partition_info_t root_part; // Linux filesystem partition candidate.
    partition_info_t data_part; // Microsoft Basic Data partition for /mnt.
    partition_info_t esp_part;  // EFI System Partition for /boot.
    bool root_found;            // Root partition found on this device.
    bool data_found;            // Data partition found on this device.
    bool esp_found;             // ESP found on this device.
};

static struct gpt_scan_state gpt_states[3];
static struct gpt_scan_state *gpt_scan_target = nullptr;

static void vfs_scan_callback(const partition_info_t *part)
{
    if (!gpt_scan_target || !part)
        return;

    const char *type = gpt_get_guid_name(part->type_guid);
    if (strcmp(type, "Microsoft Basic Data") == 0) {
        gpt_scan_target->data_part  = *part;
        gpt_scan_target->data_found = true;
        boot_message(INFO, "VFS: Found Data partition on drive %u at LBA %ld", part->drive, part->start_lba);
    } else if (strcmp(type, "EFI System Partition") == 0) {
        gpt_scan_target->esp_part  = *part;
        gpt_scan_target->esp_found = true;
        boot_message(INFO, "VFS: Found ESP partition on drive %u at LBA %ld", part->drive, part->start_lba);
    } else if (strcmp(type, "Linux Filesystem") == 0 && !gpt_scan_target->root_found) {
        gpt_scan_target->root_part  = *part;
        gpt_scan_target->root_found = true;
        boot_message(INFO, "VFS: Found Root partition on drive %u at LBA %ld", part->drive, part->start_lba);
    }
}

static void vfs_scan_device(uint8_t device)
{
    if (!storage_device_present(device))
        return;

    memset(&gpt_states[device], 0, sizeof(gpt_states[device]));
    gpt_scan_target = &gpt_states[device];
    gpt_read_partitions(device, vfs_scan_callback);
    gpt_scan_target = nullptr;
}

void vfs_mount_root(void)
{
    const uint8_t device_count = storage_device_count();
    for (uint8_t dev = 0; dev < device_count; dev++) {
        vfs_scan_device(dev);
    }

    int boot_device = -1;
    for (uint8_t dev = 0; dev < device_count; dev++) {
        if (storage_device_present(dev) && gpt_states[dev].esp_found && gpt_states[dev].root_found) {
            boot_device = dev;
            break;
        }
    }

    if (boot_device < 0) {
        for (uint8_t dev = 0; dev < device_count; dev++) {
            if (storage_device_present(dev) && gpt_states[dev].esp_found) {
                boot_device = dev;
                break;
            }
        }
    }

    if (boot_device < 0) {
        for (uint8_t dev = 0; dev < device_count; dev++) {
            if (storage_device_present(dev) && gpt_states[dev].root_found) {
                boot_device = dev;
                break;
            }
        }
    }

    if (boot_device < 0) {
        for (uint8_t dev = 0; dev < device_count; dev++) {
            if (storage_device_present(dev)) {
                boot_device = dev;
                break;
            }
        }
    }

    if (boot_device < 0) {
        boot_message(ERROR, "VFS: No storage devices detected");
        return;
    }

    boot_message(INFO,
                 "VFS: Boot device %u backend=%s",
                 (unsigned)boot_device,
                 storage_device_backend_name((uint8_t)boot_device));

    struct gpt_scan_state *boot_state = &gpt_states[boot_device];
    if (boot_state->root_found) {
        vfs_root = ext2_mount(boot_state->root_part.drive, boot_state->root_part.start_lba);
        if (vfs_root) {
            boot_message(INFO, "VFS: Mounted EXT2 on /");
        } else {
            boot_message(ERROR, "VFS: Failed to mount EXT2 on /");
        }
    }

    if (!vfs_root) {
        boot_message(WARNING,
                     "VFS: GPT mount failed or no root found, trying fallback LBA 2048 on drive %u",
                     (unsigned)boot_device);
        vfs_root = fat32_mount((uint8_t)boot_device, 2048);
        if (vfs_root) {
            boot_message(INFO, "VFS: Mounted FAT32 on / (Fallback)");
        } else {
            boot_message(ERROR, "VFS: Failed to mount FAT32");
        }
    }

    if (vfs_root && boot_state->data_found) {
        vfs_inode_t *mnt_node = vfs_finddir(vfs_root, "mnt");
        if (mnt_node) {
            kfree(mnt_node); // Free the EXT2 node, we will mount over it
            vfs_inode_t *fat_root = fat32_mount(boot_state->data_part.drive, boot_state->data_part.start_lba);
            if (fat_root) {
                vfs_register_mount("mnt", fat_root);
                boot_message(INFO, "VFS: Mounted FAT32 on /mnt");
            } else {
                boot_message(ERROR, "VFS: Failed to mount FAT32 on /mnt");
            }
        } else {
            boot_message(WARNING, "VFS: /mnt not found in root, skipping FAT32 mount");
        }
    }

    if (vfs_root && boot_state->esp_found) {
        vfs_inode_t *boot_node = vfs_finddir(vfs_root, "boot");
        if (boot_node) {
            kfree(boot_node); // replace placeholder
            vfs_inode_t *esp_root = fat32_mount(boot_state->esp_part.drive, boot_state->esp_part.start_lba);
            if (esp_root) {
                vfs_register_mount("boot", esp_root);
                boot_message(INFO, "VFS: Mounted ESP on /boot");
            } else {
                boot_message(ERROR, "VFS: Failed to mount ESP on /boot");
            }
        } else {
            boot_message(WARNING, "VFS: /boot not found in root, skipping ESP mount");
        }
    }

    if (vfs_root && device_count > 1u && storage_device_present(1) && boot_device != 1) {
        struct gpt_scan_state *disk1_state = &gpt_states[1];
        if (disk1_state->root_found) {
            vfs_inode_t *node = vfs_finddir(vfs_root, "disk1");
            if (node) {
                kfree(node); // replace placeholder
                vfs_inode_t *ext_root = ext2_mount(disk1_state->root_part.drive, disk1_state->root_part.start_lba);
                if (ext_root) {
                    vfs_register_mount("disk1", ext_root);
                    boot_message(INFO, "VFS: Mounted EXT2 on /disk1");
                } else {
                    boot_message(ERROR, "VFS: Failed to mount EXT2 on /disk1");
                }
            } else {
                boot_message(WARNING, "VFS: /disk1 not found in root, skipping disk1 mount");
            }
        }
    }

    if (vfs_root && device_count > 2u && storage_device_present(2) && boot_device != 2) {
        struct gpt_scan_state *usb_state = &gpt_states[2];
        if (usb_state->root_found) {
            vfs_inode_t *node = vfs_finddir(vfs_root, "usb");
            if (node) {
                kfree(node); // replace placeholder
                vfs_inode_t *ext_root = ext2_mount(usb_state->root_part.drive, usb_state->root_part.start_lba);
                if (ext_root) {
                    vfs_register_mount("usb", ext_root);
                    boot_message(INFO, "VFS: Mounted EXT2 on /usb");
                } else {
                    boot_message(ERROR, "VFS: Failed to mount EXT2 on /usb");
                }
            } else {
                boot_message(WARNING, "VFS: /usb not found in root, skipping USB mount");
            }
        }
    }

    // Flush buffered boot logs to disk once mounts are ready.
    boot_log_flush();
}

uint64_t vfs_read(const vfs_inode_t *node, uint64_t offset, uint64_t size, uint8_t *buffer)
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

void vfs_open(const vfs_inode_t *node)
{
    if (node->iops && node->iops->open)
        node->iops->open(node);
}

void vfs_close(vfs_inode_t *node)
{
    if (node->iops && node->iops->close)
        node->iops->close(node);
}

int vfs_poll(const vfs_inode_t *node, short events, short *revents)
{
    if (!node || !revents)
        return -1;
    if (node->iops && node->iops->poll)
        return node->iops->poll(node, events, revents);
    return -1;
}

vfs_dirent_t *vfs_readdir(const vfs_inode_t *node, uint32_t index)
{
    if ((node->flags & 0x07) == VFS_DIRECTORY && node->iops && node->iops->readdir) {
        // Try to get entry from the underlying filesystem
        vfs_dirent_t *dirent = node->iops->readdir(node, index);

        if (dirent || node != vfs_root) {
            return dirent;
        }

        // If underlying FS is done (dirent == nullptr) AND we are at root,
        // check for virtual mount points that are NOT on disk.

        // Count real entries to determine offset
        uint32_t real_count = 0;
        if (index > 0) {
            while (1) {
                vfs_dirent_t *d = node->iops->readdir(node, real_count);
                if (!d)
                    break;
                kfree(d);
                real_count++;
            }
        }

        if (index < real_count)
            return nullptr; // Should have been caught by step 1

        uint32_t virt_index   = index - real_count;
        uint32_t current_virt = 0;

        for (int i = 0; i < mount_count; i++) {
            // Check if this mount point exists on disk
            bool on_disk = false;
            if (node->iops->finddir) {
                vfs_inode_t *found = node->iops->finddir(node, mount_table[i].name);
                if (found) {
                    on_disk = true;
                    kfree(found);
                }
            }

            if (!on_disk) {
                if (current_virt == virt_index) {
                    vfs_dirent_t *virt_ent = kmalloc(sizeof(vfs_dirent_t));
                    if (!virt_ent)
                        return nullptr;
                    strncpy(virt_ent->name, mount_table[i].name, 127);
                    virt_ent->name[127] = '\0';
                    virt_ent->inode     = 0;
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
    if (!node || !name)
        return nullptr;

    if ((node->flags & 0x07) == VFS_DIRECTORY && node->iops && node->iops->finddir) {
        // Check mounts if we are at root
        if (node == vfs_root) {
            vfs_inode_t *mounted = vfs_check_mount(name);
            if (mounted) {
                return mounted;
            }
        }

        vfs_inode_t *child = node->iops->finddir(node, name);
        return child;
    }
    return nullptr;
}

static void vfs_put_inode_hold(vfs_inode_t *node, const vfs_inode_t *hold)
{
    if (!node || node == hold)
        return;
    vfs_put_inode(node);
}

static vfs_inode_t *vfs_resolve_path_hold(const char *path, const vfs_inode_t *hold)
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

    while (*path) {
        if (*path == '/') {
            name[name_idx] = 0;
            if (name_idx > 0) {
                vfs_inode_t *next = vfs_finddir(current, name);
                if (!next) {
                    vfs_put_inode_hold(current, hold);
                    return nullptr;
                }
                vfs_put_inode_hold(current, hold);
                current = next;
            }
            name_idx = 0;
        } else {
            if (name_idx < 127)
                name[name_idx++] = *path;
        }
        path++;
    }

    // Last component
    if (name_idx > 0) {
        name[name_idx]    = 0;
        vfs_inode_t *next = vfs_finddir(current, name);
        if (!next) {
            vfs_put_inode_hold(current, hold);
            return nullptr;
        }
        vfs_put_inode_hold(current, hold);
        current = next;
    }

    return current;
}

vfs_inode_t *vfs_resolve_path(const char *path)
{
    return vfs_resolve_path_hold(path, nullptr);
}

static int vfs_normalize_status(int status)
{
    if (status == 0)
        return 0;
    if (status < 0)
        return (status == -1) ? -EIO : status;
    return -EIO;
}

static int vfs_split_parent_name(const char *path, char *parent, size_t parent_size,
                                 char *name, size_t name_size)
{
    if (!path || !parent || !name || parent_size == 0 || name_size == 0)
        return -EINVAL;
    if (path[0] == '\0')
        return -EBADPATH;

    const char *last_slash = strrchr(path, '/');
    if (last_slash) {
        ptrdiff_t len = last_slash - path;
        if (len <= 0) {
            path_safe_copy(parent, parent_size, "/");
        } else {
            if ((size_t)len >= parent_size)
                return -EBADPATH;
            strncpy(parent, path, (size_t)len);
            parent[len] = '\0';
        }

        const char *base = last_slash + 1;
        if (base[0] == '\0' || strlen(base) >= name_size)
            return -EBADPATH;
        path_safe_copy(name, name_size, base);
        return ALL_OK;
    }

    path_safe_copy(parent, parent_size, "/");
    if (strlen(path) >= name_size)
        return -EBADPATH;
    path_safe_copy(name, name_size, path);
    return ALL_OK;
}

int vfs_mknod(char *path, int mode, int dev)
{
    if (!path)
        return -EINVAL;
    if (!vfs_root)
        return -EIO;
    if (strcmp(path, "/") == 0)
        return -EPERM;

    char parent_path[PATH_MAX];
    char filename[128];
    int split_status = vfs_split_parent_name(path, parent_path, sizeof(parent_path), filename, sizeof(filename));
    if (split_status != 0)
        return split_status;

    vfs_inode_t *existing = vfs_resolve_path(path);
    if (existing) {
        vfs_put_inode(existing);
        return -EINSTKN;
    }

    vfs_inode_t *parent = vfs_resolve_path(parent_path);
    if (!parent)
        return -ENOENT;

    int res = ALL_OK;
    if ((parent->flags & 0x07) != VFS_DIRECTORY) {
        res = -ENOTDIR;
    } else if (!parent->iops || !parent->iops->mknod) {
        res = -ENOTSUP;
    } else {
        res = vfs_normalize_status(parent->iops->mknod(parent, filename, mode, dev));
    }

    vfs_put_inode(parent);
    return res;
}

int vfs_link(const char *oldpath, const char *newpath)
{
    if (!oldpath || !newpath)
        return -EINVAL;
    if (!vfs_root)
        return -EIO;
    if (strcmp(newpath, "/") == 0)
        return -EPERM;

    vfs_inode_t *target = vfs_resolve_path(oldpath);
    if (!target)
        return -ENOENT;
    if ((target->flags & 0x07) == VFS_DIRECTORY) {
        vfs_put_inode(target);
        return -EPERM;
    }

    vfs_inode_t *existing = vfs_resolve_path(newpath);
    if (existing) {
        vfs_put_inode(existing);
        vfs_put_inode(target);
        return -EINSTKN;
    }

    char parent_path[PATH_MAX];
    char filename[128];
    int split_status = vfs_split_parent_name(newpath, parent_path, sizeof(parent_path), filename, sizeof(filename));
    if (split_status != 0) {
        vfs_put_inode(target);
        return split_status;
    }

    vfs_inode_t *parent = vfs_resolve_path(parent_path);
    if (!parent) {
        vfs_put_inode(target);
        return -ENOENT;
    }

    int res = ALL_OK;
    if ((parent->flags & 0x07) != VFS_DIRECTORY) {
        res = -ENOTDIR;
    } else if (!parent->iops || !parent->iops->link || parent->iops != target->iops) {
        res = -ENOTSUP;
    } else {
        res = vfs_normalize_status(parent->iops->link(parent, filename, target));
    }

    vfs_put_inode(parent);
    vfs_put_inode(target);
    return res;
}

int vfs_unlink(const char *path)
{
    if (!path)
        return -EINVAL;
    if (!vfs_root)
        return -EIO;
    if (strcmp(path, "/") == 0)
        return -EPERM;

    char parent_path[PATH_MAX];
    char filename[128];
    int split_status = vfs_split_parent_name(path, parent_path, sizeof(parent_path), filename, sizeof(filename));
    if (split_status != 0)
        return split_status;

    vfs_inode_t *target = vfs_resolve_path(path);
    if (!target)
        return -ENOENT;
    if ((target->flags & 0x07) == VFS_DIRECTORY) {
        vfs_put_inode(target);
        return -EISDIR;
    }
    vfs_put_inode(target);

    vfs_inode_t *parent = vfs_resolve_path(parent_path);
    if (!parent)
        return -ENOENT;

    int res = ALL_OK;
    if ((parent->flags & 0x07) != VFS_DIRECTORY) {
        res = -ENOTDIR;
    } else if (!parent->iops || !parent->iops->unlink) {
        res = -ENOTSUP;
    } else {
        res = vfs_normalize_status(parent->iops->unlink(parent, filename));
    }

    vfs_put_inode(parent);
    return res;
}

int vfs_rename(const char *oldpath, const char *newpath)
{
    if (!oldpath || !newpath)
        return -EINVAL;
    if (!vfs_root)
        return -EIO;
    if (strcmp(oldpath, "/") == 0 || strcmp(newpath, "/") == 0)
        return -EPERM;

    char old_parent_path[PATH_MAX];
    char old_name[128];
    int split_status = vfs_split_parent_name(oldpath,
                                             old_parent_path,
                                             sizeof(old_parent_path),
                                             old_name,
                                             sizeof(old_name));
    if (split_status != 0)
        return split_status;

    char new_parent_path[PATH_MAX];
    char new_name[128];
    split_status = vfs_split_parent_name(newpath,
                                         new_parent_path,
                                         sizeof(new_parent_path),
                                         new_name,
                                         sizeof(new_name));
    if (split_status != 0)
        return split_status;

    vfs_inode_t *old_node = vfs_resolve_path(oldpath);
    if (!old_node)
        return -ENOENT;
    if ((old_node->flags & 0x07) == VFS_DIRECTORY) {
        vfs_put_inode(old_node);
        return -EPERM;
    }
    vfs_put_inode(old_node);

    vfs_inode_t *new_node = vfs_resolve_path(newpath);
    if (new_node) {
        if ((new_node->flags & 0x07) == VFS_DIRECTORY) {
            vfs_put_inode(new_node);
            return -EISDIR;
        }
        vfs_put_inode(new_node);
    }

    if (strcmp(oldpath, newpath) == 0)
        return 0;

    vfs_inode_t *new_parent = vfs_resolve_path(new_parent_path);
    if (!new_parent)
        return -ENOENT;

    vfs_inode_t *new_parent_hold = new_parent;
    if (new_parent_hold != vfs_root) {
        if (new_parent_hold->iops && new_parent_hold->iops->clone) {
            new_parent = new_parent_hold->iops->clone(new_parent_hold);
        } else {
            new_parent = kmalloc(sizeof(vfs_inode_t));
            if (new_parent)
                memcpy(new_parent, new_parent_hold, sizeof(vfs_inode_t));
        }

        vfs_put_inode(new_parent_hold);
        if (!new_parent)
            return -ENOMEM;
    }

    vfs_inode_t *old_parent = vfs_resolve_path_hold(old_parent_path, new_parent);
    if (!old_parent) {
        vfs_put_inode(new_parent);
        return -ENOENT;
    }

    int res = ALL_OK;
    if ((old_parent->flags & 0x07) != VFS_DIRECTORY || (new_parent->flags & 0x07) != VFS_DIRECTORY) {
        res = -ENOTDIR;
    } else if (!old_parent->iops || old_parent->iops != new_parent->iops || !old_parent->iops->rename) {
        res = -ENOTSUP;
    } else {
        res = vfs_normalize_status(old_parent->iops->rename(old_parent, old_name, new_parent, new_name));
    }

    vfs_put_inode(old_parent);
    if (new_parent != old_parent)
        vfs_put_inode(new_parent);
    return res;
}
