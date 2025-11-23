#include "test.h"
#include "vfs.h"
#include "string.h"
#include "heap.h"

// Tests for FAT32 Mounted Filesystem at /mnt

TEST_PRIO(test_vfs_fat32_mount_lookup, 10)
{
    if (!vfs_root)
        return false;

    // Resolve /mnt
    vfs_inode_t *mnt = vfs_resolve_path("/mnt");
    if (!mnt)
    {
        printf("VFS: /mnt not found\n");
        return false;
    }

    // Try to find DATA_T~1.TXT in /mnt
    vfs_inode_t *file = vfs_finddir(mnt, "DATA_T~1.TXT");
    if (file)
    {
        printf("VFS: Found DATA_T~1.TXT in /mnt (Mount working)\n");
        kfree(file);
        if (mnt != vfs_root)
            kfree(mnt);
        return true;
    }
    else
    {
        printf("VFS: DATA_T~1.TXT not found in /mnt (Mount NOT working)\n");
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
        printf("VFS: /mnt not found\n");
        return false;
    }

    printf("VFS: Listing /mnt:\n");
    vfs_dirent_t *dirent;
    int i = 0;
    bool found = false;
    while ((dirent = vfs_readdir(mnt, i++)))
    {
        printf("  %s\n", dirent->name);
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
        printf("VFS: data_test.txt not found in /mnt listing\n");
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

    // "Hello Data Partition"
    bool passed = (bytes > 0 && strncmp(buffer, "Hello Data", 10) == 0);
    if (!passed)
    {
        printf("VFS FAT32: Read failed or wrong data. Got '%s', bytes: %lu\n", buffer, bytes);
    }

    kfree(file);
    kfree(mnt);
    return passed;
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
        printf("VFS FAT32: Write failed, expected %d, got %lu\n", strlen(new_data), written);
    }

    kfree(file);
    kfree(mnt);
    return passed;
}
