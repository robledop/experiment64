#include "test.h"
#include "vfs.h"
#include "string.h"
#include "console.h"
#include "heap.h"

TEST(test_console_device)
{
    vfs_inode_t *console = vfs_resolve_path("/dev/console");
    TEST_ASSERT(console != nullptr);
    TEST_ASSERT((console->flags & VFS_CHARDEVICE) != 0);
    return true;
}

TEST(test_dev_dir)
{
    vfs_inode_t *dev = vfs_resolve_path("/dev");
    TEST_ASSERT(dev != nullptr);
    TEST_ASSERT((dev->flags & VFS_DIRECTORY) != 0);

    vfs_dirent_t *dirent = vfs_readdir(dev, 0);
    TEST_ASSERT(dirent != nullptr);
    TEST_ASSERT(strcmp(dirent->name, "console") == 0);
    kfree(dirent);
    return true;
}
