#include "test.h"
#include "fat32.h"
#include "terminal.h"

extern fat32_fs_t fs;
extern bool fs_initialized;

TEST(test_fat32_stat)
{
    if (!fs_initialized)
        return false;

    fat32_file_info_t info;
    if (fat32_stat(&fs, "TEST.TXT", &info) != 0)
    {
        printf("Failed to stat TEST.TXT\n");
        return false;
    }

    printf("Stat TEST.TXT: Size=%d, Inode=%lu\n", info.size, info.inode);

    ASSERT(info.size > 0);
    ASSERT(info.inode > 0);

    // Check if inode is stable (call again)
    fat32_file_info_t info2;
    fat32_stat(&fs, "TEST.TXT", &info2);
    ASSERT(info.inode == info2.inode);

    return true;
}
