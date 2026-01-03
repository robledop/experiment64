#include "test.h"
#include "test_util.h"
#include "vfs.h"
#include "mouse.h"
// These tests are intentionally "non-interactive": we can't rely on PS/2 hardware events
// during automated runs. We only validate devfs wiring and that basic APIs don't crash.

TEST(test_mouse_device_exists)
{
    vfs_inode_t *mouse = vfs_resolve_path("/dev/mouse");
    TEST_ASSERT(mouse != nullptr);
    const bool ok = (mouse->flags & VFS_CHARDEVICE) != 0;
    vfs_release(mouse);
    TEST_ASSERT(ok);
    return true;
}

TEST(test_mouse_device_has_read_op)
{
    vfs_inode_t *mouse = vfs_resolve_path("/dev/mouse");
    TEST_ASSERT(mouse != nullptr);
    const bool has_iops = (mouse->iops != nullptr);
    const bool has_read = has_iops && (mouse->iops->read != nullptr);
    vfs_release(mouse);
    TEST_ASSERT(has_iops);
    TEST_ASSERT(has_read);
    return true;
}

TEST(test_mouse_flush_pending_events_smoke)
{
    // Should be safe to call even if no events have occurred.
    mouse_flush_pending_events();
    return true;
}

