#include "test.h"
#include "vfs.h"
#include "fat32.h"
#include "string.h"
#include "heap.h"

// Mirror of the private struct in fat32.c so we can grab the mounted fs pointer
typedef struct
{
    fat32_fs_t *fs;
    uint32_t dir_cluster;
    uint32_t dir_offset;
} fat32_inode_data_t;

// Tests for FAT32 Mounted Filesystem at /mnt

TEST_PRIO(test_vfs_fat32_mount_lookup, 10)
{
    if (!vfs_root)
        return false;

    // Resolve /mnt
    vfs_inode_t *mnt = vfs_resolve_path("/mnt");
    if (!mnt)
    {
        printk("VFS: /mnt not found\n");
        return false;
    }

    // Try to find DATA_T~1.TXT in /mnt
    vfs_inode_t *file = vfs_finddir(mnt, "DATA_T~1.TXT");
    if (file)
    {
        printk("VFS: Found DATA_T~1.TXT in /mnt (Mount working)\n");
        kfree(file);
        if (mnt != vfs_root)
            kfree(mnt);
        return true;
    }
    else
    {
        printk("VFS: DATA_T~1.TXT not found in /mnt (Mount NOT working)\n");
        if (mnt != vfs_root)
            kfree(mnt);
        return false;
    }
}

TEST_PRIO(test_vfs_fat32_mount_readdir, 20)
{
    vfs_inode_t *mnt = vfs_finddir(vfs_root, "mnt");
    if (!mnt)
    {
        printk("VFS: /mnt not found\n");
        return false;
    }

    printk("VFS: Listing /mnt:\n");
    vfs_dirent_t *dirent;
    int i = 0;
    bool found = false;
    while ((dirent = vfs_readdir(mnt, i++)))
    {
        printk("  %s\n", dirent->name);
        if (strcmp(dirent->name, "DATA_T~1.TXT") == 0)
        {
            found = true;
        }
        kfree(dirent);
    }

    if (mnt != vfs_root)
        kfree(mnt);

    if (!found)
    {
        printk("VFS: data_test.txt not found in /mnt listing\n");
        return false;
    }
    return true;
}

TEST_PRIO(test_vfs_fat32_read, 30)
{
    vfs_inode_t *mnt = vfs_resolve_path("/mnt");
    if (!mnt)
        return false;

    vfs_inode_t *file = vfs_finddir(mnt, "DATA_T~1.TXT");
    if (!file)
    {
        kfree(mnt);
        return false;
    }

    char buffer[32];
    memset(buffer, 0, 32);
    uint64_t bytes = vfs_read(file, 0, 32, (uint8_t *)buffer);

    // Accept either original "Hello Data..." content or an updated "FAT32Write..." prefix
    bool passed = (bytes > 0 &&
                   (strncmp(buffer, "Hello Data", 10) == 0 ||
                    strncmp(buffer, "FAT32Write", 10) == 0));
    if (!passed)
    {
        printk("VFS FAT32: Read failed or wrong data. Got '%s', bytes: %lu\n", buffer, bytes);
    }

    kfree(file);
    kfree(mnt);
    return passed;
}

TEST_PRIO(test_vfs_fat32_link_not_supported, 60)
{
    vfs_inode_t *mnt = vfs_resolve_path("/mnt");
    if (!mnt)
        return false;

    const char *src = "/mnt/DATA_T~1.TXT";
    const char *dst = "/mnt/FAT32_LINK.TST";

    vfs_unlink((char *)dst);

    int res = vfs_link(src, dst);
    bool passed = (res != 0);

    vfs_inode_t *maybe = vfs_resolve_path(dst);
    if (maybe)
    {
        // Unexpectedly created a link; clean up and mark failure.
        vfs_unlink((char *)dst);
        passed = false;
        kfree(maybe);
    }

    if (mnt != vfs_root)
        kfree(mnt);
    return passed;
}

TEST_PRIO(test_vfs_fat32_unlink_file, 70)
{
    vfs_inode_t *mnt = vfs_resolve_path("/mnt");
    if (!mnt)
        return false;

    fat32_inode_data_t *mnt_data = (fat32_inode_data_t *)mnt->device;
    TEST_ASSERT(mnt_data != nullptr);
    fat32_fs_t *fs = mnt_data->fs;
    TEST_ASSERT(fs != nullptr);

    const char *fname = "UNLINK.TST";
    const char *full_path = "/mnt/UNLINK.TST";

    // Remove leftovers if any.
    vfs_unlink((char *)full_path);

    TEST_ASSERT(fat32_create_file(fs, fname) == 0);

    vfs_inode_t *node = vfs_resolve_path(full_path);
    TEST_ASSERT(node != nullptr);

    const char *payload = "fat32_unlink_payload";
    TEST_ASSERT(vfs_write(node, 0, strlen(payload), (uint8_t *)payload) == strlen(payload));

    TEST_ASSERT(vfs_unlink(full_path) == 0);
    TEST_ASSERT(vfs_resolve_path(full_path) == nullptr);

    // Confirm removal from FAT metadata as well.
    fat32_file_info_t info;
    TEST_ASSERT(fat32_stat(fs, fname, &info) != 0);

    kfree(node);
    if (mnt != vfs_root)
        kfree(mnt);
    return true;
}

TEST_PRIO(test_vfs_fat32_mknod_touch, 65)
{
    if (!vfs_root)
        return false;

    const char *path = "/mnt/TOUCH.TST";
    vfs_unlink((char *)path); // clean up any leftovers

    TEST_ASSERT(vfs_mknod((char *)path, VFS_FILE, 0) == 0);

    vfs_inode_t *node = vfs_resolve_path(path);
    TEST_ASSERT(node != nullptr);

    const char *payload = "touch_fat32";
    TEST_ASSERT(vfs_write(node, 0, strlen(payload), (uint8_t *)payload) == strlen(payload));

    char buf[16] = {0};
    TEST_ASSERT(vfs_read(node, 0, strlen(payload), (uint8_t *)buf) == strlen(payload));
    TEST_ASSERT(strncmp(buf, payload, strlen(payload)) == 0);

    TEST_ASSERT(vfs_unlink(path) == 0);
    TEST_ASSERT(vfs_resolve_path(path) == nullptr);

    kfree(node);
    return true;
}

TEST_PRIO(test_vfs_fat32_zero_length_read, 45)
{
    vfs_inode_t *mnt = vfs_resolve_path("/mnt");
    if (!mnt)
        return false;

    vfs_inode_t *file = vfs_finddir(mnt, "DATA_T~1.TXT");
    if (!file)
    {
        kfree(mnt);
        return false;
    }

    char buffer[4] = {0};
    uint64_t bytes = vfs_read(file, 0, 0, (uint8_t *)buffer);
    bool passed = (bytes == 0);

    kfree(file);
    kfree(mnt);
    return passed;
}

TEST_PRIO(test_vfs_fat32_long_chain_rw, 50)
{
    vfs_inode_t *mnt = vfs_resolve_path("/mnt");
    if (!mnt)
        return false;
    fat32_inode_data_t *mnt_data = (fat32_inode_data_t *)mnt->device;
    TEST_ASSERT(mnt_data != nullptr);
    fat32_fs_t *fs = mnt_data->fs;
    TEST_ASSERT(fs != nullptr);

    const char *fname = "LONG.BIN";
    const size_t data_size = 256 * 1024; // 256 KiB to cross many clusters
    uint8_t *data = kmalloc(data_size);
    TEST_ASSERT(data != nullptr);
    for (size_t i = 0; i < data_size; i++)
        data[i] = (uint8_t)(i & 0xFF);

    vfs_inode_t *file = vfs_finddir(mnt, (char *)fname);
    if (file)
    {
        fat32_delete_file(fs, fname);
        kfree(file);
    }

    // Create the file via FAT32 helpers (VFS lacks mknod for FAT32). If it already exists,
    // just proceed with the existing entry.
    int create_res = fat32_create_file(fs, fname);
    if (create_res != 0)
    {
        fat32_file_info_t info;
        TEST_ASSERT(fat32_stat(fs, fname, &info) == 0);
    }

    file = vfs_finddir(mnt, (char *)fname);
    TEST_ASSERT(file != nullptr);

    uint64_t written = vfs_write(file, 0, data_size, data);
    TEST_ASSERT(written == data_size);

    uint8_t *readback = kmalloc(data_size);
    TEST_ASSERT(readback != nullptr);
    uint64_t read_bytes = vfs_read(file, 0, data_size, readback);
    TEST_ASSERT(read_bytes == data_size);
    TEST_ASSERT(readback[0] == data[0] && readback[data_size - 1] == data[data_size - 1]);

    kfree(readback);
    fat32_delete_file(fs, fname);
    kfree(file);
    kfree(mnt);
    kfree(data);
    return true;
}

TEST_PRIO(test_vfs_fat32_write, 40)
{
    vfs_inode_t *mnt = vfs_resolve_path("/mnt");
    if (!mnt)
        return false;

    vfs_inode_t *file = vfs_finddir(mnt, "DATA_T~1.TXT");
    if (!file)
    {
        kfree(mnt);
        return false;
    }

    const char *new_data = "FAT32Write";
    uint64_t written = vfs_write(file, 0, strlen(new_data), (uint8_t *)new_data);

    bool passed = (written == strlen(new_data));
    if (!passed)
    {
        printk("VFS FAT32: Write failed, expected %d, got %lu\n", strlen(new_data), written);
    }

    kfree(file);
    kfree(mnt);
    return passed;
}
