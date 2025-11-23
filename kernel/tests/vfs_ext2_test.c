#include "test.h"
#include "vfs.h"
#include "string.h"
#include "heap.h"

// Tests for EXT2 Root Filesystem

TEST(test_vfs_ext2_open)
{
    if (!vfs_root)
    {
        printf("VFS: Root not initialized\n");
        return false;
    }

    vfs_inode_t *file = vfs_finddir(vfs_root, "test.txt");
    if (!file)
    {
        printf("VFS: Failed to find test.txt\n");
        return false;
    }

    vfs_open(file);

    kfree(file);
    return true;
}

TEST_PRIO(test_vfs_ext2_write, 20)
{
    if (!vfs_root)
        return false;

    vfs_inode_t *file = vfs_finddir(vfs_root, "test.txt");
    if (!file)
        return false;

    const char *new_data = "WriteTest";
    uint64_t written = vfs_write(file, 0, strlen(new_data), (uint8_t *)new_data);

    bool passed = (written == strlen(new_data));
    if (!passed)
    {
        printf("VFS: Write failed, expected %d, got %lu\n", strlen(new_data), written);
    }

    kfree(file);
    return passed;
}

TEST_PRIO(test_vfs_ext2_read, 30)
{
    if (!vfs_root)
        return false;

    vfs_inode_t *file = vfs_finddir(vfs_root, "test.txt");
    if (!file)
        return false;

    char buffer[32];
    memset(buffer, 0, 32);
    uint64_t bytes = vfs_read(file, 0, 32, (uint8_t *)buffer);

    // "WriteTest" overwrote start of file
    bool passed = (bytes >= 9 && strncmp(buffer, "WriteTest", 9) == 0);
    if (!passed)
    {
        printf("VFS: Read failed or wrong data. Got '%s', bytes: %lu\n", buffer, bytes);
    }

    kfree(file);
    return passed;
}

TEST(test_vfs_ext2_close)
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

TEST_PRIO(test_vfs_ext2_basic, 10)
{
    if (!vfs_root)
    {
        printf("VFS: Root not initialized\n");
        return false;
    }

    // Test finddir
    vfs_inode_t *file = vfs_finddir(vfs_root, "test.txt");
    if (!file)
    {
        printf("VFS: Failed to find 'test.txt'\n");
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
        printf("VFS: Read returned 0 bytes\n");
        kfree(file);
        return false;
    }

    // "WriteTest" might be there if test_vfs_ext2_write ran first.
    // But if we run basic first, it should be "Hello Ext2"
    // We should probably reset the file or check for either.

    if (strncmp(buffer, "Hello Ext2", 10) != 0 && strncmp(buffer, "WriteTest", 9) != 0)
    {
        printf("VFS: Read wrong data: '%s'\n", buffer);
        kfree(file);
        return false;
    }

    // Test close
    vfs_close(file);

    printf("VFS: Basic test passed. Read: %s", buffer); // buffer has newline
    kfree(file);
    return true;
}
