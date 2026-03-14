#include <tests/test.h>
#include <tests/test_util.h>
#include <fs/vfs.h>
#include <lib/string.h>
#include <lib/path.h>
#include <io/storage.h>
#include <drivers/terminal.h>
#include <mem/heap.h>

// Generic VFS tests (operating on vfs_root)

TEST(test_vfs_generic_open)
{
    if (!vfs_root)
    {
        printk("VFS: Root not initialized\n");
        return false;
    }

    vfs_inode_t *file = vfs_finddir(vfs_root, "test.txt");
    if (!file)
    {
        printk("VFS: Failed to find test.txt\n");
        return false;
    }

    vfs_open(file);

    vfs_release(file);
    return true;
}

TEST(test_vfs_generic_write)
{
    if (!vfs_root)
        return false;

    vfs_inode_t *file = vfs_finddir(vfs_root, "test.txt");
    if (!file)
        return false;

    const char *new_data = "GenWrite";
    const uint64_t written = vfs_write(file, 0, strlen(new_data), (uint8_t *)new_data);

    const bool passed = (written == strlen(new_data));
    if (!passed)
    {
        printk("VFS: Write failed, expected %d, got %lu\n", strlen(new_data), written);
    }

    vfs_release(file);
    return passed;
}

TEST(test_vfs_generic_read)
{
    if (!vfs_root)
        return false;

    vfs_inode_t *file = vfs_finddir(vfs_root, "test.txt");
    if (!file)
        return false;

    char buffer[32] = {0};
    const uint64_t bytes = vfs_read(file, 0, 32, (uint8_t *)buffer);

    // "GenWrite" might have overwritten start of file if write ran first
    // Or "WriteTest" from ext2 test
    // Or "Hello Ext2"

    printk("VFS Generic Read: Got '%s'\n", buffer);

    const bool passed = (bytes > 0);

    vfs_release(file);
    return passed;
}

TEST(test_vfs_generic_close)
{
    if (!vfs_root)
        return false;

    vfs_inode_t *file = vfs_finddir(vfs_root, "test.txt");
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

    vfs_inode_t *file = vfs_finddir(vfs_root, "test.txt");
    if (!file)
    {
        printk("VFS: Failed to find 'test.txt'\n");
        return false;
    }

    vfs_open(file);

    char buffer[32] = {0};
    const uint64_t bytes = vfs_read(file, 0, 32, (uint8_t *)buffer);

    if (bytes == 0)
    {
        printk("VFS: Read returned 0 bytes\n");
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
    vfs_mknod("/readdir_dummy.txt", VFS_FILE, 0);

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
        return true;

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
