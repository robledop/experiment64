#include "test.h"
#include "vfs.h"
#include "string.h"
#include "heap.h"

// Generic VFS tests (operating on vfs_root)

TEST(test_vfs_generic_open)
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
        printf("VFS: Write failed, expected %d, got %lu\n", strlen(new_data), written);
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

    printf("VFS Generic Read: Got '%s'\n", buffer);

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

    // Test close
    vfs_close(file);

    printf("VFS: Basic test passed. Read: %s", buffer);
    kfree(file);
    return true;
}
