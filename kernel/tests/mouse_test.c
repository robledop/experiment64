#include "test.h"
#include "vfs.h"
#include "mouse.h"
// These tests are intentionally "non-interactive": we can't rely on PS/2 hardware events
// during automated runs. We only validate devfs wiring and that basic APIs don't crash.

TEST(test_mouse_device_exists)
{
    const vfs_inode_t *mouse = vfs_resolve_path("/dev/mouse");
    TEST_ASSERT(mouse != nullptr);
    TEST_ASSERT((mouse->flags & VFS_CHARDEVICE) != 0);
    return true;
}

TEST(test_mouse_device_has_read_op)
{
    const vfs_inode_t *mouse = vfs_resolve_path("/dev/mouse");
    TEST_ASSERT(mouse != nullptr);
    TEST_ASSERT(mouse->iops != nullptr);
    TEST_ASSERT(mouse->iops->read != nullptr);
    return true;
}

TEST(test_mouse_flush_pending_events_smoke)
{
    // Should be safe to call even if no events have occurred.
    mouse_flush_pending_events();
    return true;
}


