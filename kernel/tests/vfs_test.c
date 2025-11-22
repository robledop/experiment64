#include "test.h"
#include "vfs.h"
#include "string.h"
#include "heap.h"

// We assume vfs_root is already mounted to FAT32 in kernel.c

TEST(test_vfs_open)
{
    if (!vfs_root)
    {
        printf("VFS: Root not initialized\n");
        return false;
    }

    vfs_inode_t *file = vfs_finddir(vfs_root, "TEST.TXT");
    if (!file)
    {
        printf("VFS: Failed to find TEST.TXT\n");
        return false;
    }

    vfs_open(file);

    kfree(file);
    return true;
}

TEST(test_vfs_write)
{
    if (!vfs_root)
        return false;

    vfs_inode_t *file = vfs_finddir(vfs_root, "TEST.TXT");
    if (!file)
        return false;

    const char *new_data = "WriteTest";
    uint64_t written = vfs_write(file, 0, strlen(new_data), (uint8_t *)new_data);

    bool passed = (written == 0);
    if (!passed)
    {
        printf("VFS: Write should return 0 (unimplemented), got %lu\n", written);
    }

    kfree(file);
    return passed;
}

TEST(test_vfs_read)
{
    if (!vfs_root)
        return false;

    vfs_inode_t *file = vfs_finddir(vfs_root, "TEST.TXT");
    if (!file)
        return false;

    char buffer[32];
    memset(buffer, 0, 32);
    uint64_t bytes = vfs_read(file, 0, 32, (uint8_t *)buffer);

    // "Hello FAT32\n" is 12 bytes
    bool passed = (bytes >= 11 && strncmp(buffer, "Hello FAT32", 11) == 0);
    if (!passed)
    {
        printf("VFS: Read failed or wrong data. Got '%s', bytes: %lu\n", buffer, bytes);
    }

    kfree(file);
    return passed;
}

TEST(test_vfs_close)
{
    if (!vfs_root)
        return false;

    vfs_inode_t *file = vfs_finddir(vfs_root, "TEST.TXT");
    if (!file)
        return false;

    vfs_open(file);
    vfs_close(file);

    kfree(file);
    return true;
}

TEST(test_vfs_basic)
{
    if (!vfs_root)
    {
        printf("VFS: Root not initialized\n");
        return false;
    }

    // Test finddir
    vfs_inode_t *file = vfs_finddir(vfs_root, "TEST.TXT");
    if (!file)
    {
        printf("VFS: Failed to find 'TEST.TXT'\n");
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

    if (strncmp(buffer, "Hello FAT32", 11) != 0)
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
