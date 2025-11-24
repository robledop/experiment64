#include "test.h"
#include "vfs.h"
#include "string.h"
#include "heap.h"

// Helper to construct paths
static void make_path(char *buffer, const char *base, const char *sub, int index)
{
    char num[16];
    snprintk(num, 16, "%d", index);
    strcpy(buffer, base);
    if (buffer[strlen(buffer) - 1] != '/')
        strcat(buffer, "/");
    strcat(buffer, sub);
    strcat(buffer, num);
}

TEST(test_ext2_stress)
{
    if (!vfs_root)
    {
        printk("VFS: Root not initialized\n");
        return false;
    }

    const char *dirname = "/stress_ext2";
    printk("Creating EXT2 stress directory %s...\n", dirname);

    // Create directory
    if (vfs_mknod((char *)dirname, VFS_DIRECTORY, 0) != 0)
    {
        // It might already exist from previous run, try to continue
        // But vfs_mknod returns -1 if exists.
        // We can check if it exists.
        vfs_inode_t *node = vfs_resolve_path(dirname);
        if (!node)
        {
            printk("Failed to create directory %s\n", dirname);
            return false;
        }
        kfree(node);
        printk("Directory %s already exists, continuing...\n", dirname);
    }

    int num_subdirs = 2;
    int files_per_subdir = 2;
    char path[128];
    char content[64];

    // 1. Create Subdirectories and Files
    for (int i = 0; i < num_subdirs; i++)
    {
        make_path(path, dirname, "dir", i);

        if (vfs_mknod(path, VFS_DIRECTORY, 0) != 0)
        {
            // Check if exists
            vfs_inode_t *node = vfs_resolve_path(path);
            if (!node)
            {
                printk("Failed to create directory %s\n", path);
                return false;
            }
            kfree(node);
        }

        for (int j = 0; j < files_per_subdir; j++)
        {
            char filepath[128];
            make_path(filepath, path, "file", j);
            strcat(filepath, ".txt");

            // Create file
            if (vfs_mknod(filepath, VFS_FILE, 0) != 0)
            {
                // Check if exists
                vfs_inode_t *node = vfs_resolve_path(filepath);
                if (!node)
                {
                    printk("Failed to create file %s\n", filepath);
                    return false;
                }
                kfree(node);
            }

            // Write content
            snprintk(content, sizeof(content), "Data %d-%d", i, j);

            vfs_inode_t *file = vfs_resolve_path(filepath);
            if (!file)
            {
                printk("Failed to resolve file %s for writing\n", filepath);
                return false;
            }

            // Open? vfs_write doesn't strictly require open in this implementation but good practice
            vfs_open(file);
            if (vfs_write(file, 0, strlen(content), (uint8_t *)content) != strlen(content))
            {
                printk("Failed to write to %s\n", filepath);
                vfs_close(file);
                kfree(file);
                return false;
            }
            vfs_close(file);
            kfree(file);
        }
    }

    printk("EXT2 Stress: Created %d directories and %d files.\n", num_subdirs, num_subdirs * files_per_subdir);

    // 2. Verify Content
    for (int i = 0; i < num_subdirs; i++)
    {
        make_path(path, dirname, "dir", i);

        for (int j = 0; j < files_per_subdir; j++)
        {
            char filepath[128];
            make_path(filepath, path, "file", j);
            strcat(filepath, ".txt");

            vfs_inode_t *file = vfs_resolve_path(filepath);
            if (!file)
            {
                printk("Failed to resolve file %s for reading\n", filepath);
                return false;
            }

            char buffer[64];
            memset(buffer, 0, sizeof(buffer));

            vfs_open(file);
            vfs_read(file, 0, sizeof(buffer), (uint8_t *)buffer);
            vfs_close(file);
            kfree(file);

            snprintk(content, sizeof(content), "Data %d-%d", i, j);
            if (strncmp(buffer, content, strlen(content)) != 0)
            {
                printk("Content mismatch in %s: expected '%s', got '%s'\n", filepath, content, buffer);
                return false;
            }
        }
    }

    printk("EXT2 Stress: Verification passed.\n");
    return true;
}
