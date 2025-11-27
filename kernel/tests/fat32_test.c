#include "test.h"
#include "fat32.h"
#include "ide.h"
#include "string.h"
#include "terminal.h"

fat32_fs_t test_fs;
bool fs_initialized = false;

TEST_PRIO(test_fat32_init, 0)
{
    // Data partition starts at 95MiB (LBA 194560) per scripts/make_image.sh.
    if (fat32_init(&test_fs, 0, 194560) == 0)
    {
        fs_initialized = true;
        return true;
    }
    return false;
}

TEST(test_fat32_list)
{
    if (!fs_initialized)
        return false;
    fat32_list_dir(&test_fs, "/");
    return true;
}

TEST(test_fat32_read_file)
{
    if (!fs_initialized)
        return false;

    uint8_t buffer[512] = {0};

    // We added test.txt with "Hello FAT32" in the Makefile
    int res = fat32_read_file(&test_fs, "TEST.TXT", buffer, 512);

    if (res != 0)
    {
        printk("Failed to read TEST.TXT, error: %d\n", res);
        return false;
    }

    printk("File content: %s\n", buffer);

    if (strncmp((char *)buffer, "Hello FAT32", 11) == 0)
    {
        return true;
    }

    return false;
}

// test_fat32_stat is in fat32_stat_test.c

TEST(test_fat32_write_delete)
{
    if (!fs_initialized)
        return false;

    const char *filename = "NEW.TXT";
    const char *content = "Write Test";
    uint32_t len = strlen(content);

    // 1. Create and Write
    if (fat32_write_file(&test_fs, filename, (uint8_t *)content, len) != 0)
    {
        printk("Failed to write NEW.TXT\n");
        return false;
    }

    // 2. Read back
    uint8_t buffer[512];
    memset(buffer, 0, 512);
    if (fat32_read_file(&test_fs, filename, buffer, 512) != 0)
    {
        printk("Failed to read back NEW.TXT\n");
        return false;
    }

    if (strncmp((char *)buffer, content, len) != 0)
    {
        printk("Content mismatch: expected '%s', got '%s'\n", content, buffer);
        return false;
    }

    // 3. Delete
    if (fat32_delete_file(&test_fs, filename) != 0)
    {
        printk("Failed to delete NEW.TXT\n");
        return false;
    }

    // 4. Verify deletion
    fat32_file_info_t info;
    if (fat32_stat(&test_fs, filename, &info) == 0)
    {
        printk("NEW.TXT still exists after deletion\n");
        return false;
    }

    return true;
}
