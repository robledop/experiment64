#include "test.h"
#include "fat32.h"
#include "string.h"
#include "terminal.h"
#include "heap.h"

extern fat32_fs_t test_fs;
extern bool fs_initialized;

#include "test.h"
#include "fat32.h"
#include "string.h"
#include "terminal.h"
#include "heap.h"

extern fat32_fs_t test_fs;
extern bool fs_initialized;

#define STRESS_FILE_COUNT 50
#define STRESS_FILE_SIZE 1024

TEST(test_fat32_stress)
{
    if (!fs_initialized)
        return false;

    char *dirname = "/STRESS";
    printk("Creating stress directory %s...\n", dirname);

    // Try to create directory, ignore if it exists (though VFS might error)
    if (vfs_mknod(dirname, VFS_DIRECTORY, 0) != 0)
    {
        // If it fails, it might already exist, which is fine for stress test re-runs
        // But for clean test, we assume it succeeds or we check if it exists.
        // For now, let's assume failure is bad unless we can check existence.
        vfs_inode_t *dir = vfs_resolve_path(dirname);
        if (!dir)
        {
            printk("Failed to create directory %s\n", dirname);
            return false;
        }
    }

    uint8_t *write_buf = (uint8_t *)kmalloc(STRESS_FILE_SIZE);
    uint8_t *read_buf = (uint8_t *)kmalloc(STRESS_FILE_SIZE);

    if (!write_buf || !read_buf)
    {
        printk("Failed to allocate buffers\n");
        return false;
    }

    // 1. Create and Write Phase
    printk("Phase 1: Creating and Writing %d files...\n", STRESS_FILE_COUNT);
    for (int i = 0; i < STRESS_FILE_COUNT; i++)
    {
        char filename[64];
        snprintf(filename, sizeof(filename), "%s/FILE%d.TXT", dirname, i);

        // Create file
        if (vfs_mknod(filename, VFS_FILE, 0) != 0)
        {
            printk("Failed to create file %s\n", filename);
            return false;
        }

        vfs_inode_t *node = vfs_resolve_path(filename);
        if (!node)
        {
            printk("Failed to resolve file %s\n", filename);
            return false;
        }

        vfs_open(node);

        // Fill buffer with pattern based on index
        memset(write_buf, (i % 255), STRESS_FILE_SIZE);

        uint64_t written = vfs_write(node, 0, STRESS_FILE_SIZE, write_buf);
        if (written != STRESS_FILE_SIZE)
        {
            printk("Failed to write all data to %s (wrote %llu)\n", filename, written);
            vfs_close(node);
            return false;
        }

        vfs_close(node);

        if (i % 10 == 0)
            printk(".");
    }
    printk("\n");

    // 2. Read and Verify Phase
    printk("Phase 2: Reading and Verifying...\n");
    for (int i = 0; i < STRESS_FILE_COUNT; i++)
    {
        char filename[64];
        snprintf(filename, sizeof(filename), "%s/FILE%d.TXT", dirname, i);

        vfs_inode_t *node = vfs_resolve_path(filename);
        if (!node)
        {
            printk("Failed to resolve file %s for reading\n", filename);
            return false;
        }

        vfs_open(node);

        memset(read_buf, 0, STRESS_FILE_SIZE);
        uint64_t read = vfs_read(node, 0, STRESS_FILE_SIZE, read_buf);
        if (read != STRESS_FILE_SIZE)
        {
            printk("Failed to read all data from %s (read %llu)\n", filename, read);
            vfs_close(node);
            return false;
        }

        // Verify
        memset(write_buf, (i % 255), STRESS_FILE_SIZE);
        if (memcmp(write_buf, read_buf, STRESS_FILE_SIZE) != 0)
        {
            printk("Data verification failed for %s\n", filename);
            vfs_close(node);
            return false;
        }

        vfs_close(node);
        if (i % 10 == 0)
            printk(".");
    }
    printk("\nStress test completed successfully.\n");

    kfree(write_buf);
    kfree(read_buf);
    return true;
}