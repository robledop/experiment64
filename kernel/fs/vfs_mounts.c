#include <fs/vfs.h>
#include <drivers/gpt.h>
#include <drivers/terminal.h>
#include <fs/ext2.h>
#include <fs/fat32.h>
#include <io/storage.h>
#include <lib/string.h>
#include <mem/heap.h>

struct mount_point
{
    char name[64];
    vfs_inode_t *root;
    bool is_virtual;
};

static struct mount_point mount_table[16];
static int mount_count = 0;
static int root_real_count_cache = -1;

typedef vfs_inode_t *(*vfs_mount_fn_t)(uint8_t drive_index, uint32_t partition_lba);

static vfs_inode_t *vfs_check_mount(const char *name)
{
    for (int i = 0; i < mount_count; i++) {
        if (strcmp(mount_table[i].name, name) != 0)
            continue;

        vfs_inode_t *root = mount_table[i].root;
        if (root && root->iops && root->iops->clone)
            return root->iops->clone(root);

        vfs_inode_t *copy = kmalloc(sizeof(vfs_inode_t));
        if (copy)
            memcpy(copy, mount_table[i].root, sizeof(vfs_inode_t));
        return copy;
    }
    return nullptr;
}

void vfs_register_mount(const char *name, vfs_inode_t *root)
{
    if (mount_count >= 16)
        return;

    strncpy(mount_table[mount_count].name, name, 63);
    mount_table[mount_count].name[63] = '\0';
    mount_table[mount_count].root     = root;

    bool on_disk = false;
    if (vfs_root && vfs_root->iops && vfs_root->iops->finddir) {
        vfs_inode_t *found = vfs_root->iops->finddir(vfs_root, name);
        if (found) {
            on_disk = true;
            vfs_release(found);
        }
    }

    mount_table[mount_count].is_virtual = !on_disk;
    mount_count++;
    root_real_count_cache = -1;
}

vfs_dirent_t *vfs_readdir(const vfs_inode_t *node, uint32_t index)
{
    if ((node->flags & VFS_TYPE_MASK) != VFS_DIRECTORY || !node->iops || !node->iops->readdir)
        return nullptr;

    vfs_dirent_t *dirent = node->iops->readdir(node, index);
    if (dirent || node != vfs_root)
        return dirent;

    if (root_real_count_cache < 0) {
        uint32_t count = 0;
        while (1) {
            vfs_dirent_t *entry = node->iops->readdir(node, count);
            if (!entry)
                break;
            kfree(entry);
            count++;
        }
        root_real_count_cache = (int)count;
    }

    const uint32_t real_count = (uint32_t)root_real_count_cache;
    if (index < real_count)
        return nullptr;

    uint32_t virt_index   = index - real_count;
    uint32_t current_virt = 0;
    for (int i = 0; i < mount_count; i++) {
        if (!mount_table[i].is_virtual)
            continue;
        if (current_virt != virt_index) {
            current_virt++;
            continue;
        }

        vfs_dirent_t *virt_ent = kmalloc(sizeof(vfs_dirent_t));
        if (!virt_ent)
            return nullptr;
        strncpy(virt_ent->name, mount_table[i].name, 127);
        virt_ent->name[127] = '\0';
        virt_ent->inode     = 0;
        return virt_ent;
    }

    return nullptr;
}

vfs_inode_t *vfs_finddir(vfs_inode_t *node, const char *name)
{
    if (!node || !name)
        return nullptr;
    if ((node->flags & VFS_TYPE_MASK) != VFS_DIRECTORY || !node->iops || !node->iops->finddir)
        return nullptr;

    if (node == vfs_root) {
        vfs_inode_t *mounted = vfs_check_mount(name);
        if (mounted)
            return mounted;
    }

    return node->iops->finddir(node, name);
}

struct gpt_scan_state
{
    partition_info_t root_part;
    partition_info_t data_part;
    partition_info_t esp_part;
    bool root_found;
    bool data_found;
    bool esp_found;
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
    if (device >= (sizeof(gpt_states) / sizeof(gpt_states[0])))
        return;
    if (!storage_device_present(device))
        return;

    memset(&gpt_states[device], 0, sizeof(gpt_states[device]));
    gpt_scan_target = &gpt_states[device];
    gpt_read_partitions(device, vfs_scan_callback);
    gpt_scan_target = nullptr;
}

static bool vfs_state_has_boot_parts(const struct gpt_scan_state *state)
{
    return state && state->esp_found && state->root_found;
}

static bool vfs_state_has_esp(const struct gpt_scan_state *state)
{
    return state && state->esp_found;
}

static bool vfs_state_has_root(const struct gpt_scan_state *state)
{
    return state && state->root_found;
}

static int vfs_find_matching_device(uint8_t device_count, bool (*predicate)(const struct gpt_scan_state *))
{
    for (uint8_t dev = 0; dev < device_count; dev++) {
        if (!storage_device_present(dev))
            continue;
        if (!predicate || predicate(&gpt_states[dev]))
            return dev;
    }
    return -1;
}

static int vfs_select_boot_device(uint8_t device_count)
{
    static bool (*const predicates[])(const struct gpt_scan_state *) = {
        vfs_state_has_boot_parts,
        vfs_state_has_esp,
        vfs_state_has_root,
        nullptr,
    };

    for (size_t i = 0; i < sizeof(predicates) / sizeof(predicates[0]); i++) {
        int dev = vfs_find_matching_device(device_count, predicates[i]);
        if (dev >= 0)
            return dev;
    }

    return -1;
}

static bool vfs_mount_named_partition(const char *mount_name, const char *label,
                                      const partition_info_t *part, vfs_mount_fn_t mount_fn)
{
    vfs_inode_t *placeholder = vfs_finddir(vfs_root, mount_name);
    if (!placeholder) {
        boot_message(WARNING, "VFS: /%s not found in root, skipping %s mount", mount_name, label);
        return false;
    }

    vfs_release(placeholder);

    vfs_inode_t *mounted = mount_fn(part->drive, part->start_lba);
    if (!mounted) {
        boot_message(ERROR, "VFS: Failed to mount %s on /%s", label, mount_name);
        return false;
    }

    vfs_register_mount(mount_name, mounted);
    boot_message(INFO, "VFS: Mounted %s on /%s", label, mount_name);
    return true;
}

static void vfs_mount_extra_root_device(uint8_t device, int boot_device, const char *mount_name)
{
    if (!vfs_root || device == boot_device || !storage_device_present(device))
        return;

    struct gpt_scan_state *state = &gpt_states[device];
    if (!state->root_found)
        return;

    vfs_mount_named_partition(mount_name, "EXT2", &state->root_part, ext2_mount);
}

void vfs_mount_root(void)
{
    const uint8_t device_count = storage_device_count();
    const uint8_t scan_count = device_count < (sizeof(gpt_states) / sizeof(gpt_states[0]))
        ? device_count
        : (uint8_t)(sizeof(gpt_states) / sizeof(gpt_states[0]));

    for (uint8_t dev = 0; dev < scan_count; dev++) {
        vfs_scan_device(dev);
    }

    int boot_device = vfs_select_boot_device(scan_count);
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

    if (vfs_root && boot_state->data_found)
        vfs_mount_named_partition("mnt", "FAT32", &boot_state->data_part, fat32_mount);
    if (vfs_root && boot_state->esp_found)
        vfs_mount_named_partition("boot", "ESP", &boot_state->esp_part, fat32_mount);
    if (scan_count > 1u)
        vfs_mount_extra_root_device(1, boot_device, "disk1");
    if (scan_count > 2u)
        vfs_mount_extra_root_device(2, boot_device, "usb");

    boot_log_flush();
}
