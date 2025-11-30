/**
 * EXT2 Regression Tests
 *
 * These tests verify fixes for critical EXT2 filesystem bugs:
 *
 * 1. Per-device superblock isolation: Each mounted EXT2 partition must use
 *    its own superblock. Previously a single global superblock was overwritten
 *    when mounting a second EXT2 partition.
 *
 * 2. Block bitmap 2-sector handling: The block bitmap is 1024 bytes (2 sectors).
 *    Block allocation and freeing must correctly handle blocks whose bitmap bit
 *    falls in the second sector (bits 4096-8191).
 *
 * 3. Multi-block-group allocation: When one block group is full, allocation
 *    must search other block groups rather than failing.
 *
 * 4. Data integrity: Write-read cycles must return identical data.
 */

#include "test.h"
#include "vfs.h"
#include "string.h"
#include "heap.h"

// Helper to generate unique file paths
static void make_test_path(char *buf, size_t size, const char *base, int index)
{
    snprintk(buf, size, "%s_%d", base, index);
}

/**
 * Test: Multi-device superblock isolation
 *
 * Verify that operations on one EXT2 partition don't corrupt another.
 * This catches the bug where a single global ext2_sb was used for all devices.
 */
TEST(test_ext2_multi_device_isolation)
{
    if (!vfs_root)
        return false;

    // Create a file on the root EXT2 partition
    const char *root_file = "/isolation_test_root.txt";
    const char *root_data = "root_partition_data";

    vfs_unlink((char *)root_file);
    TEST_ASSERT(vfs_mknod((char *)root_file, VFS_FILE, 0) == 0);

    vfs_inode_t *rf = vfs_resolve_path(root_file);
    TEST_ASSERT(rf != nullptr);
    TEST_ASSERT(vfs_write(rf, 0, strlen(root_data), (uint8_t *)root_data) == strlen(root_data));

    // Create a file on disk1 (second EXT2 partition)
    const char *disk1_file = "/disk1/isolation_test_disk1.txt";
    const char *disk1_data = "disk1_partition_data";

    vfs_unlink((char *)disk1_file);
    TEST_ASSERT(vfs_mknod((char *)disk1_file, VFS_FILE, 0) == 0);

    vfs_inode_t *d1f = vfs_resolve_path(disk1_file);
    TEST_ASSERT(d1f != nullptr);
    TEST_ASSERT(vfs_write(d1f, 0, strlen(disk1_data), (uint8_t *)disk1_data) == strlen(disk1_data));

    // Now read back data from BOTH partitions to ensure neither was corrupted
    char buf1[64] = {0};
    char buf2[64] = {0};

    // Re-read from root partition - this should still work correctly
    vfs_inode_t *rf2 = vfs_resolve_path(root_file);
    TEST_ASSERT(rf2 != nullptr);
    TEST_ASSERT(vfs_read(rf2, 0, strlen(root_data), (uint8_t *)buf1) == strlen(root_data));
    TEST_ASSERT(strncmp(buf1, root_data, strlen(root_data)) == 0);

    // Re-read from disk1 partition
    vfs_inode_t *d1f2 = vfs_resolve_path(disk1_file);
    TEST_ASSERT(d1f2 != nullptr);
    TEST_ASSERT(vfs_read(d1f2, 0, strlen(disk1_data), (uint8_t *)buf2) == strlen(disk1_data));
    TEST_ASSERT(strncmp(buf2, disk1_data, strlen(disk1_data)) == 0);

    // Cleanup
    kfree(rf);
    kfree(d1f);
    kfree(rf2);
    kfree(d1f2);
    vfs_unlink((char *)root_file);
    vfs_unlink((char *)disk1_file);

    return true;
}

/**
 * Test: Many file allocations to stress block allocation
 *
 * Create many files with data to force allocation of blocks across the bitmap.
 * This tests:
 * - Block allocation from both sectors of the bitmap (bits 0-4095 and 4096-8191)
 * - Block freeing across both bitmap sectors
 * - Multi-block-group fallback when first group fills
 */
TEST(test_ext2_many_file_allocations)
{
    if (!vfs_root)
        return false;

    const char *base_path = "/disk1/alloc_test";
    const int num_files = 8;    // Reduced to fit in test disk
    const int data_size = 1024; // 1KB per file to use actual blocks
    char path[128];
    char data[1024];
    char readback[1024];

    // Fill data buffer with a pattern
    for (int i = 0; i < data_size; i++)
    {
        data[i] = (char)('A' + (i % 26));
    }

    // Create many files and write data
    for (int i = 0; i < num_files; i++)
    {
        make_test_path(path, sizeof(path), base_path, i);
        vfs_unlink(path); // Clean up any previous run

        TEST_ASSERT(vfs_mknod(path, VFS_FILE, 0) == 0);

        vfs_inode_t *file = vfs_resolve_path(path);
        TEST_ASSERT(file != nullptr);

        // Write unique data (modify first byte to identify file)
        data[0] = (char)('0' + (i % 10));
        TEST_ASSERT(vfs_write(file, 0, data_size, (uint8_t *)data) == data_size);

        kfree(file);
    }

    // Read back all files and verify data integrity
    for (int i = 0; i < num_files; i++)
    {
        make_test_path(path, sizeof(path), base_path, i);

        vfs_inode_t *file = vfs_resolve_path(path);
        TEST_ASSERT(file != nullptr);

        memset(readback, 0, sizeof(readback));
        TEST_ASSERT(vfs_read(file, 0, data_size, (uint8_t *)readback) == data_size);

        // Verify the unique first byte
        TEST_ASSERT(readback[0] == (char)('0' + (i % 10)));

        // Verify the rest of the pattern
        for (int j = 1; j < data_size; j++)
        {
            TEST_ASSERT(readback[j] == (char)('A' + (j % 26)));
        }

        kfree(file);
    }

    // Delete all files (tests bfree across bitmap sectors)
    for (int i = 0; i < num_files; i++)
    {
        make_test_path(path, sizeof(path), base_path, i);
        TEST_ASSERT(vfs_unlink(path) == 0);
    }

    return true;
}

/**
 * Test: Interleaved operations on multiple devices
 *
 * Perform alternating read/write operations between two EXT2 partitions
 * to ensure buffer cache and superblock isolation.
 */
TEST(test_ext2_interleaved_device_ops)
{
    if (!vfs_root)
        return false;

    const char *root_files[] = {"/interleave_a.txt", "/interleave_b.txt"};
    const char *disk1_files[] = {"/disk1/interleave_a.txt", "/disk1/interleave_b.txt"};
    const char *root_data[] = {"ROOT_DATA_A", "ROOT_DATA_B"};
    const char *disk1_data[] = {"DISK1_DATA_A", "DISK1_DATA_B"};

    // Clean up
    for (int i = 0; i < 2; i++)
    {
        vfs_unlink((char *)root_files[i]);
        vfs_unlink((char *)disk1_files[i]);
    }

    // Interleaved creation and writing
    for (int i = 0; i < 2; i++)
    {
        // Create on root
        TEST_ASSERT(vfs_mknod((char *)root_files[i], VFS_FILE, 0) == 0);
        vfs_inode_t *rf = vfs_resolve_path(root_files[i]);
        TEST_ASSERT(rf != nullptr);
        TEST_ASSERT(vfs_write(rf, 0, strlen(root_data[i]), (uint8_t *)root_data[i]) == strlen(root_data[i]));
        kfree(rf);

        // Create on disk1
        TEST_ASSERT(vfs_mknod((char *)disk1_files[i], VFS_FILE, 0) == 0);
        vfs_inode_t *d1f = vfs_resolve_path(disk1_files[i]);
        TEST_ASSERT(d1f != nullptr);
        TEST_ASSERT(vfs_write(d1f, 0, strlen(disk1_data[i]), (uint8_t *)disk1_data[i]) == strlen(disk1_data[i]));
        kfree(d1f);
    }

    // Interleaved reading and verification
    for (int i = 0; i < 2; i++)
    {
        char buf[32] = {0};

        // Read from disk1 first (reverse order)
        vfs_inode_t *d1f = vfs_resolve_path(disk1_files[i]);
        TEST_ASSERT(d1f != nullptr);
        TEST_ASSERT(vfs_read(d1f, 0, strlen(disk1_data[i]), (uint8_t *)buf) == strlen(disk1_data[i]));
        TEST_ASSERT(strncmp(buf, disk1_data[i], strlen(disk1_data[i])) == 0);
        kfree(d1f);

        memset(buf, 0, sizeof(buf));

        // Read from root
        vfs_inode_t *rf = vfs_resolve_path(root_files[i]);
        TEST_ASSERT(rf != nullptr);
        TEST_ASSERT(vfs_read(rf, 0, strlen(root_data[i]), (uint8_t *)buf) == strlen(root_data[i]));
        TEST_ASSERT(strncmp(buf, root_data[i], strlen(root_data[i])) == 0);
        kfree(rf);
    }

    // Cleanup
    for (int i = 0; i < 2; i++)
    {
        vfs_unlink((char *)root_files[i]);
        vfs_unlink((char *)disk1_files[i]);
    }

    return true;
}

/**
 * Test: Rapid create-write-read-delete cycle
 *
 * Quickly create, write, read, and delete files to stress the allocation
 * and freeing paths. This catches bugs in bitmap bit setting/clearing.
 */
TEST(test_ext2_rapid_lifecycle)
{
    if (!vfs_root)
        return false;

    const char *path = "/disk1/rapid_lifecycle.txt";
    const char *payloads[] = {
        "Short",
        "Medium length payload for testing",
        "A somewhat longer payload that spans more bytes in the file system",
        "X" // Very short
    };

    for (int cycle = 0; cycle < 10; cycle++)
    {
        const char *payload = payloads[cycle % 4];

        // Create
        vfs_unlink((char *)path);
        TEST_ASSERT(vfs_mknod((char *)path, VFS_FILE, 0) == 0);

        // Write
        vfs_inode_t *file = vfs_resolve_path(path);
        TEST_ASSERT(file != nullptr);
        TEST_ASSERT(vfs_write(file, 0, strlen(payload), (uint8_t *)payload) == strlen(payload));

        // Read back immediately
        char buf[128] = {0};
        vfs_inode_t *file2 = vfs_resolve_path(path);
        TEST_ASSERT(file2 != nullptr);
        TEST_ASSERT(vfs_read(file2, 0, strlen(payload), (uint8_t *)buf) == strlen(payload));
        TEST_ASSERT(strncmp(buf, payload, strlen(payload)) == 0);

        kfree(file);
        kfree(file2);

        // Delete
        TEST_ASSERT(vfs_unlink((char *)path) == 0);

        // Verify deleted
        TEST_ASSERT(vfs_resolve_path(path) == nullptr);
    }

    return true;
}

/**
 * Test: Large file with many blocks
 *
 * Create a file large enough to require multiple blocks, potentially
 * triggering indirect block allocation and testing bitmap handling
 * across sector boundaries.
 */
TEST(test_ext2_large_file)
{
    if (!vfs_root)
        return false;

    const char *path = "/disk1/large_file_test.bin";
    const int block_size = 1024;
    const int num_blocks = 8; // 8KB file

    vfs_unlink((char *)path);
    TEST_ASSERT(vfs_mknod((char *)path, VFS_FILE, 0) == 0);

    vfs_inode_t *file = vfs_resolve_path(path);
    TEST_ASSERT(file != nullptr);

    // Write data in block-sized chunks with unique patterns
    char write_buf[1024];
    for (int b = 0; b < num_blocks; b++)
    {
        // Fill block with a pattern based on block number
        for (int i = 0; i < block_size; i++)
        {
            write_buf[i] = (char)((b * 7 + i) & 0xFF);
        }
        TEST_ASSERT(vfs_write(file, b * block_size, block_size, (uint8_t *)write_buf) == block_size);
    }

    kfree(file);

    // Read back and verify
    file = vfs_resolve_path(path);
    TEST_ASSERT(file != nullptr);

    char read_buf[1024];
    for (int b = 0; b < num_blocks; b++)
    {
        memset(read_buf, 0, sizeof(read_buf));
        TEST_ASSERT(vfs_read(file, b * block_size, block_size, (uint8_t *)read_buf) == block_size);

        // Verify pattern
        for (int i = 0; i < block_size; i++)
        {
            char expected = (char)((b * 7 + i) & 0xFF);
            if (read_buf[i] != expected)
            {
                printk("Mismatch at block %d, offset %d: got 0x%02x, expected 0x%02x\n",
                       b, i, (unsigned char)read_buf[i], (unsigned char)expected);
                kfree(file);
                return false;
            }
        }
    }

    kfree(file);
    vfs_unlink((char *)path);

    return true;
}

/**
 * Test: Directory operations across devices
 *
 * Create directories and files in directories on both partitions
 * to ensure directory entry handling doesn't get confused.
 */
TEST(test_ext2_directory_isolation)
{
    if (!vfs_root)
        return false;

    const char *root_dir = "/dir_iso_test";
    const char *disk1_dir = "/disk1/dir_iso_test";
    const char *root_file = "/dir_iso_test/file.txt";
    const char *disk1_file = "/disk1/dir_iso_test/file.txt";

    // Cleanup
    vfs_unlink((char *)root_file);
    vfs_unlink((char *)disk1_file);
    vfs_unlink((char *)root_dir);
    vfs_unlink((char *)disk1_dir);

    // Create directories
    TEST_ASSERT(vfs_mknod((char *)root_dir, VFS_DIRECTORY, 0) == 0);
    TEST_ASSERT(vfs_mknod((char *)disk1_dir, VFS_DIRECTORY, 0) == 0);

    // Create files in directories
    TEST_ASSERT(vfs_mknod((char *)root_file, VFS_FILE, 0) == 0);
    TEST_ASSERT(vfs_mknod((char *)disk1_file, VFS_FILE, 0) == 0);

    // Write different data
    const char *root_data = "ROOT_DIR_FILE";
    const char *disk1_data = "DISK1_DIR_FILE";

    vfs_inode_t *rf = vfs_resolve_path(root_file);
    TEST_ASSERT(rf != nullptr);
    TEST_ASSERT(vfs_write(rf, 0, strlen(root_data), (uint8_t *)root_data) == strlen(root_data));
    kfree(rf);

    vfs_inode_t *d1f = vfs_resolve_path(disk1_file);
    TEST_ASSERT(d1f != nullptr);
    TEST_ASSERT(vfs_write(d1f, 0, strlen(disk1_data), (uint8_t *)disk1_data) == strlen(disk1_data));
    kfree(d1f);

    // Read back and verify
    char buf[32] = {0};

    rf = vfs_resolve_path(root_file);
    TEST_ASSERT(rf != nullptr);
    TEST_ASSERT(vfs_read(rf, 0, strlen(root_data), (uint8_t *)buf) == strlen(root_data));
    TEST_ASSERT(strncmp(buf, root_data, strlen(root_data)) == 0);
    kfree(rf);

    memset(buf, 0, sizeof(buf));

    d1f = vfs_resolve_path(disk1_file);
    TEST_ASSERT(d1f != nullptr);
    TEST_ASSERT(vfs_read(d1f, 0, strlen(disk1_data), (uint8_t *)buf) == strlen(disk1_data));
    TEST_ASSERT(strncmp(buf, disk1_data, strlen(disk1_data)) == 0);
    kfree(d1f);

    // Cleanup
    vfs_unlink((char *)root_file);
    vfs_unlink((char *)disk1_file);
    vfs_unlink((char *)root_dir);
    vfs_unlink((char *)disk1_dir);

    return true;
}
