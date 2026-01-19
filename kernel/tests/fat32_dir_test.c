#include <tests/test.h>
#include <fs/fat32.h>
#include <lib/string.h>
#include <drivers/terminal.h>

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

    // Create Directory
    printk("Creating directory %s...\n", dirname);
    const int dir_res = fat32_create_dir(&test_fs, dirname);
    if (dir_res != 0)
    {
        // Treat EEXIST-equivalent as success so the test is idempotent
        fat32_file_info_t info;
        if (fat32_stat(&test_fs, dirname, &info) != 0 || (info.attributes & ATTR_DIRECTORY) == 0)
        {
            printk("Failed to create directory %s\n", dirname);
            return false;
        }
    }

    printk("Writing file %s...\n", filename);
    if (fat32_write_file(&test_fs, filename, (uint8_t *)content, len) != 0)
    {
        printk("Failed to write file %s\n", filename);
        return false;
    }

    printk("Reading file %s...\n", filename);
    uint8_t buffer[512] = {0};
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

    printk("Listing directory %s...\n", dirname);
    fat32_list_dir(&test_fs, dirname);

    printk("Deleting file %s...\n", filename);
    if (fat32_delete_file(&test_fs, filename) != 0)
    {
        printk("Failed to delete file %s\n", filename);
        return false;
    }

    // Verify File Deletion
    fat32_file_info_t info;
    if (fat32_stat(&test_fs, filename, &info) == 0)
    {
        printk("File %s still exists after deletion\n", filename);
        return false;
    }

    return true;
}

TEST(test_fat32_directory_cluster_spill_and_delete)
{
    if (!fs_initialized)
        return false;

    const char *dirname = "EDGEDIR";
    // Create directory; if exists, continue using it.
    fat32_create_dir(&test_fs, dirname);

    char filename[32];
    constexpr int file_count = 24; // >16 entries to spill into the next cluster
    // Clean directory from previous runs
    for (int i = 0; i < file_count; i++)
    {
        snprintk(filename, sizeof(filename), "EDGEDIR/F%d.TXT", i);
        fat32_delete_file(&test_fs, filename);
    }

    for (int i = 0; i < file_count; i++)
    {
        snprintk(filename, sizeof(filename), "EDGEDIR/F%d.TXT", i);
        uint8_t payload = (uint8_t)(i & 0xFF);
        TEST_ASSERT(fat32_write_file(&test_fs, filename, &payload, 1) == 0);
        fat32_file_info_t info;
        TEST_ASSERT(fat32_stat(&test_fs, filename, &info) == 0);
    }

    // Delete a middle and last entry, ensure stat fails afterward.
    TEST_ASSERT(fat32_delete_file(&test_fs, "EDGEDIR/F5.TXT") == 0);
    fat32_file_info_t info;
    TEST_ASSERT(fat32_stat(&test_fs, "EDGEDIR/F5.TXT", &info) != 0);

    TEST_ASSERT(fat32_delete_file(&test_fs, "EDGEDIR/F23.TXT") == 0);
    TEST_ASSERT(fat32_stat(&test_fs, "EDGEDIR/F23.TXT", &info) != 0);

    // Clean up remaining files
    for (int i = 0; i < file_count; i++)
    {
        if (i == 5 || i == 23)
            continue;
        snprintk(filename, sizeof(filename), "EDGEDIR/F%d.TXT", i);
        fat32_delete_file(&test_fs, filename);
    }

    return true;
}

TEST(test_fat32_long_name_rejected)
{
    if (!fs_initialized)
        return false;

    const char *longname = "VERY_LONG_FILE_NAME_THAT_SHOULD_FAIL.TXT";
    const int res = fat32_write_file(&test_fs, longname, (uint8_t *)"x", 1);
    TEST_ASSERT(res != 0); // Short-name implementation should reject long names.
    return true;
}
