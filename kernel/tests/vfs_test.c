#include <tests/test.h>
#include <tests/test_util.h>
#include <fs/vfs.h>
#include <lib/string.h>
#include <lib/path.h>
#include <io/storage.h>
#include <drivers/terminal.h>
#include <mem/heap.h>

// Generic VFS tests (operating on vfs_root)

static constexpr char g_vfs_seed_name[] = "test.txt";
static constexpr char g_vfs_seed_prefix[] = "Hello Ext2";

static bool vfs_write_and_verify_scratch_file(const char *path, const char *payload)
{
    char buffer[64] = {0};
    bool passed = false;

    vfs_unlink((char *)path);

    if (vfs_mknod((char *)path, VFS_FILE, 0) != 0)
    {
        printk("VFS: Failed to create %s\n", path);
        return false;
    }

    vfs_inode_t *file = vfs_resolve_path(path);
    if (!file)
    {
        printk("VFS: Failed to resolve %s\n", path);
        vfs_unlink((char *)path);
        return false;
    }

    const size_t len = strlen(payload);
    if (vfs_write(file, 0, len, (uint8_t *)payload) != len)
    {
        printk("VFS: Write failed for %s\n", path);
        goto out;
    }

    if (vfs_read(file, 0, len, (uint8_t *)buffer) != len)
    {
        printk("VFS: Readback failed for %s\n", path);
        goto out;
    }

    if (strncmp(buffer, payload, len) != 0)
    {
        printk("VFS: Readback mismatch for %s\n", path);
        goto out;
    }

    passed = true;

out:
    vfs_release(file);
    if (vfs_unlink((char *)path) != 0)
    {
        printk("VFS: Cleanup failed for %s\n", path);
        passed = false;
    }
    return passed;
}

TEST(test_vfs_generic_open)
{
    if (!vfs_root)
    {
        printk("VFS: Root not initialized\n");
        return false;
    }

    vfs_inode_t *file = vfs_finddir(vfs_root, g_vfs_seed_name);
    if (!file)
    {
        printk("VFS: Failed to find %s\n", g_vfs_seed_name);
        return false;
    }

    vfs_open(file);
    vfs_close(file);

    vfs_release(file);
    return true;
}

TEST(test_vfs_generic_write)
{
    if (!vfs_root)
        return false;

    return vfs_write_and_verify_scratch_file("/vfs_generic_write.txt", "GenWrite");
}

TEST(test_vfs_generic_read)
{
    if (!vfs_root)
        return false;

    vfs_inode_t *file = vfs_finddir(vfs_root, g_vfs_seed_name);
    if (!file)
        return false;

    char buffer[32] = {0};
    const uint64_t bytes = vfs_read(file, 0, 32, (uint8_t *)buffer);
    const size_t expected_len = strlen(g_vfs_seed_prefix);
    const bool passed = (bytes >= expected_len && strncmp(buffer, g_vfs_seed_prefix, expected_len) == 0);

    vfs_release(file);
    return passed;
}

TEST(test_vfs_generic_close)
{
    if (!vfs_root)
        return false;

    vfs_inode_t *file = vfs_finddir(vfs_root, g_vfs_seed_name);
    if (!file)
        return false;

    vfs_open(file);
    vfs_close(file);

    vfs_release(file);
    return true;
}

TEST(test_vfs_generic_basic)
{
    if (!vfs_root)
    {
        printk("VFS: Root not initialized\n");
        return false;
    }

    vfs_inode_t *file = vfs_finddir(vfs_root, g_vfs_seed_name);
    if (!file)
    {
        printk("VFS: Failed to find '%s'\n", g_vfs_seed_name);
        return false;
    }

    vfs_open(file);

    char buffer[32] = {0};
    const uint64_t bytes = vfs_read(file, 0, 32, (uint8_t *)buffer);

    const size_t expected_len = strlen(g_vfs_seed_prefix);
    if (bytes < expected_len)
    {
        printk("VFS: Read returned %lu bytes\n", bytes);
        vfs_release(file);
        return false;
    }

    if (strncmp(buffer, g_vfs_seed_prefix, expected_len) != 0)
    {
        printk("VFS: Read wrong data: '%s'\n", buffer);
        vfs_release(file);
        return false;
    }

    vfs_close(file);

    printk("VFS: Basic test passed. Read: %s", buffer);
    vfs_release(file);
    return true;
}

TEST(test_vfs_root_readdir)
{
    if (!vfs_root)
        return false;

    // Ensure a known entry exists.
    const char *name = "readdir_dummy.txt";
    vfs_unlink("/readdir_dummy.txt");
    TEST_ASSERT(vfs_mknod("/readdir_dummy.txt", VFS_FILE, 0) == 0);

    bool found = false;
    for (uint32_t i = 0;; i++)
    {
        vfs_dirent_t *dent = vfs_readdir(vfs_root, i);
        if (!dent)
            break;
        if (strncmp(dent->name, name, sizeof(dent->name)) == 0)
        {
            found = true;
            kfree(dent);
            break;
        }
        kfree(dent);
    }

    if (!found)
    {
        printk("VFS: %s not found in root readdir\n", name);
    }

    if (vfs_unlink("/readdir_dummy.txt") != 0)
    {
        printk("VFS: Failed to remove %s\n", name);
        return false;
    }

    return found;
}

TEST(test_vfs_path_canonicalization)
{
    // test.txt is seeded into rootfs; resolve via dotted path segments.
    vfs_inode_t *node = vfs_resolve_path("/./test.txt");
    TEST_ASSERT(node != nullptr);
    vfs_release(node);

    node = vfs_resolve_path("/bin/../test.txt");
    TEST_ASSERT(node != nullptr);
    vfs_release(node);
    return true;
}

TEST(test_vfs_path_overlength_rejected)
{
    // Build an overlength path (> PATH_MAX) and ensure resolution fails.
    char longpath[PATH_MAX + 16];
    memset(longpath, 'a', sizeof(longpath));
    longpath[0] = '/';
    longpath[sizeof(longpath) - 1] = '\0';
    vfs_inode_t *node = vfs_resolve_path(longpath);
    TEST_ASSERT(node == nullptr);
    return true;
}

TEST(test_vfs_usb_mount)
{
    if (!storage_device_present(2))
    {
        printk("VFS: USB storage device missing\n");
        return false;
    }

    vfs_inode_t *node = vfs_resolve_path("/usb");
    if (!node)
    {
        printk("VFS: /usb not mounted\n");
        return false;
    }

    const bool is_dir = ((node->flags & VFS_TYPE_MASK) == VFS_DIRECTORY);
    if (!is_dir)
    {
        printk("VFS: /usb is not a directory\n");
    }

    vfs_release(node);
    return is_dir;
}
