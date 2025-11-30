#include "test.h"
#include "vfs.h"
#include "string.h"
#include "heap.h"

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

    kfree(file);
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
    uint64_t written = vfs_write(file, 0, strlen(new_data), (uint8_t *)new_data);

    bool passed = (written == strlen(new_data));
    if (!passed)
    {
        printk("VFS: Write failed, expected %d, got %lu\n", strlen(new_data), written);
    }

    kfree(file);
    return passed;
}

TEST(test_vfs_generic_read)
{
    if (!vfs_root)
        return false;

    vfs_inode_t *file = vfs_finddir(vfs_root, "test.txt");
    if (!file)
        return false;

    char buffer[32];
    memset(buffer, 0, 32);
    uint64_t bytes = vfs_read(file, 0, 32, (uint8_t *)buffer);

    // "GenWrite" might have overwritten start of file if write ran first
    // Or "WriteTest" from ext2 test
    // Or "Hello Ext2"

    printk("VFS Generic Read: Got '%s'\n", buffer);

    bool passed = (bytes > 0);

    kfree(file);
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

    kfree(file);
    return true;
}

TEST(test_vfs_generic_basic)
{
    if (!vfs_root)
    {
        printk("VFS: Root not initialized\n");
        return false;
    }

    // Test finddir
    vfs_inode_t *file = vfs_finddir(vfs_root, "test.txt");
    if (!file)
    {
        printk("VFS: Failed to find 'test.txt'\n");
        return false;
    }

    // Test open
    vfs_open(file);

    // Test read
    char buffer[32];
    memset(buffer, 0, 32);
    uint64_t bytes = vfs_read(file, 0, 32, (uint8_t *)buffer);

    if (bytes == 0)
    {
        printk("VFS: Read returned 0 bytes\n");
        kfree(file);
        return false;
    }

    // Test close
    vfs_close(file);

    printk("VFS: Basic test passed. Read: %s", buffer);
    kfree(file);
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
    kfree(node);

    node = vfs_resolve_path("/bin/../test.txt");
    TEST_ASSERT(node != nullptr);
    kfree(node);
    return true;
}

TEST(test_vfs_path_overlength_rejected)
{
    // Build an overlength path (> VFS_MAX_PATH) and ensure resolution fails.
    char longpath[VFS_MAX_PATH + 16];
    memset(longpath, 'a', sizeof(longpath));
    longpath[0] = '/';
    longpath[sizeof(longpath) - 1] = '\0';
    vfs_inode_t *node = vfs_resolve_path(longpath);
    TEST_ASSERT(node == nullptr);
    return true;
}
