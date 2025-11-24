#include "test.h"
#include "fat32.h"
#include "string.h"
#include "terminal.h"

extern fat32_fs_t test_fs;
extern bool fs_initialized;

TEST(test_fat32_directories)
{
    if (!fs_initialized)
        return false;

    const char *dirname = "SUBDIR";
    const char *filename = "SUBDIR/FILE.TXT";
    const char *content = "Nested File Content";
    uint32_t len = strlen(content);

    // 1. Create Directory
    printk("Creating directory %s...\n", dirname);
    if (fat32_create_dir(&test_fs, dirname) != 0)
    {
        printk("Failed to create directory %s\n", dirname);
        return false;
    }

    // 2. Create File in Directory
    printk("Writing file %s...\n", filename);
    if (fat32_write_file(&test_fs, filename, (uint8_t *)content, len) != 0)
    {
        printk("Failed to write file %s\n", filename);
        return false;
    }

    // 3. Read File from Directory
    printk("Reading file %s...\n", filename);
    uint8_t buffer[512];
    memset(buffer, 0, 512);
    if (fat32_read_file(&test_fs, filename, buffer, 512) != 0)
    {
        printk("Failed to read file %s\n", filename);
        return false;
    }

    if (strncmp((char *)buffer, content, len) != 0)
    {
        printk("Content mismatch: expected '%s', got '%s'\n", content, buffer);
        return false;
    }

    // 4. List Directory
    printk("Listing directory %s...\n", dirname);
    fat32_list_dir(&test_fs, dirname);

    // 5. Delete File
    printk("Deleting file %s...\n", filename);
    if (fat32_delete_file(&test_fs, filename) != 0)
    {
        printk("Failed to delete file %s\n", filename);
        return false;
    }

    // 6. Verify File Deletion
    fat32_file_info_t info;
    if (fat32_stat(&test_fs, filename, &info) == 0)
    {
        printk("File %s still exists after deletion\n", filename);
        return false;
    }

    return true;
}
