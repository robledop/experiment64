#include <tests/test.h>
#include <tests/test_util.h>
#include <fs/vfs.h>
#include <lib/string.h>

TEST(test_console_device)
{
    vfs_inode_t *console = vfs_resolve_path("/dev/console");
    TEST_ASSERT(console != nullptr);
    const bool ok = (console->flags & VFS_CHARDEVICE) != 0;
    vfs_release(console);
    TEST_ASSERT(ok);
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
    vfs_release(dev);
    return true;
}
