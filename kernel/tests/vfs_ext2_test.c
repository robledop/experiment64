#include <tests/test.h>
#include <tests/test_util.h>
#include <fs/vfs.h>
#include <lib/string.h>
#include <drivers/terminal.h>

static constexpr char g_ext2_seed_name[] = "test.txt";
static constexpr char g_ext2_seed_prefix[] = "Hello Ext2";

static bool ext2_write_and_verify_scratch_file(const char *path, const char *payload)
{
    char buffer[64] = {0};
    bool passed = false;

    vfs_unlink((char *)path);

    if (vfs_mknod((char *)path, VFS_FILE, 0) != 0)
    {
        printk("VFS: failed to create %s\n", path);
        return false;
    }

    vfs_inode_t *file = vfs_resolve_path(path);
    if (!file)
    {
        printk("VFS: failed to resolve %s\n", path);
        vfs_unlink((char *)path);
        return false;
    }

    const size_t len = strlen(payload);
    if (vfs_write(file, 0, len, (uint8_t *)payload) != len)
    {
        printk("VFS: failed to write %s\n", path);
        goto out;
    }

    if (vfs_read(file, 0, len, (uint8_t *)buffer) != len)
    {
        printk("VFS: failed to read back %s\n", path);
        goto out;
    }

    if (strncmp(buffer, payload, len) != 0)
    {
        printk("VFS: readback mismatch for %s\n", path);
        goto out;
    }

    passed = true;

out:
    vfs_release(file);
    if (vfs_unlink((char *)path) != 0)
    {
        printk("VFS: failed to remove %s\n", path);
        passed = false;
    }
    return passed;
}

TEST(test_vfs_ext2_open)
{
    if (!vfs_root)
    {
        printk("VFS: Root not initialized\n");
        return false;
    }

    vfs_inode_t *file = vfs_finddir(vfs_root, g_ext2_seed_name);
    if (!file)
    {
        printk("VFS: Failed to find %s\n", g_ext2_seed_name);
        return false;
    }

    vfs_open(file);
    vfs_close(file);

    vfs_release(file);
    return true;
}

TEST_PRIO(test_vfs_ext2_write, 20)
{
    if (!vfs_root)
        return false;

    return ext2_write_and_verify_scratch_file("/ext2_write_test.txt", "WriteTest");
}

TEST_PRIO(test_vfs_ext2_read, 30)
{
    if (!vfs_root)
        return false;

    vfs_inode_t *file = vfs_finddir(vfs_root, g_ext2_seed_name);
    if (!file)
        return false;

    char buffer[32] = {0};
    const uint64_t bytes = vfs_read(file, 0, 32, (uint8_t *)buffer);
    const size_t expected_len = strlen(g_ext2_seed_prefix);

    bool passed = (bytes >= expected_len && strncmp(buffer, g_ext2_seed_prefix, expected_len) == 0);
    if (!passed)
    {
        printk("VFS: Read failed or wrong data. Got '%s', bytes: %lu\n", buffer, bytes);
    }

    vfs_release(file);
    return passed;
}

TEST(test_vfs_ext2_close)
{
    if (!vfs_root)
        return false;

    vfs_inode_t *file = vfs_finddir(vfs_root, "test.txt");
    if (!file)
        return false;

    vfs_open(file);
    vfs_close(file);

    vfs_release(file);
    return true;
}

TEST_PRIO(test_vfs_ext2_basic, 10)
{
    if (!vfs_root)
    {
        printk("VFS: Root not initialized\n");
        return false;
    }

    vfs_inode_t *file = vfs_finddir(vfs_root, g_ext2_seed_name);
    if (!file)
    {
        printk("VFS: Failed to find '%s'\n", g_ext2_seed_name);
        return false;
    }

    vfs_open(file);

    char buffer[32] = {0};
    const uint64_t bytes = vfs_read(file, 0, 32, (uint8_t *)buffer);

    const size_t expected_len = strlen(g_ext2_seed_prefix);
    if (bytes < expected_len)
    {
        printk("VFS: Read returned %lu bytes\n", bytes);
        vfs_release(file);
        return false;
    }

    if (strncmp(buffer, g_ext2_seed_prefix, expected_len) != 0)
    {
        printk("VFS: Read wrong data: '%s'\n", buffer);
        vfs_release(file);
        return false;
    }

    vfs_close(file);

    printk("VFS: Basic test passed. Read: %s", buffer);
    vfs_release(file);
    return true;
}

TEST(test_ext2_long_name_and_duplicate_rejection)
{
    if (!vfs_root)
        return false;

    char long_component[80];
    memset(long_component, 'l', sizeof(long_component) - 1);
    long_component[sizeof(long_component) - 1] = '\0';

    char path[256];
    snprintk(path, sizeof(path), "/ext2_%s", long_component);

    vfs_unlink(path);

    int res = vfs_mknod(path, VFS_FILE, 0);
    if (res != 0)
    {
        printk("VFS: failed to create long name path %s\n", path);
        return false;
    }

    // Duplicate creation should fail.
    TEST_ASSERT(vfs_mknod(path, VFS_FILE, 0) != 0);

    vfs_inode_t *node = vfs_resolve_path(path);
    TEST_ASSERT(node != nullptr);
    vfs_release(node);
    TEST_ASSERT(vfs_unlink(path) == 0);
    return true;
}

TEST_PRIO(test_ext2_link_and_unlink, 40)
{
    if (!vfs_root)
        return false;

    const char *src_path = "/ext2_link_src";
    const char *link_path = "/ext2_link_dst";

    // Clean up any previous leftovers.
    vfs_unlink((char *)link_path);
    vfs_unlink((char *)src_path);

    TEST_ASSERT(vfs_mknod((char *)src_path, VFS_FILE, 0) == 0);

    vfs_inode_t *src = vfs_resolve_path(src_path);
    TEST_ASSERT(src != nullptr);

    const char *payload = "ext2_link_payload";
    TEST_ASSERT(vfs_write(src, 0, strlen(payload), (uint8_t *)payload) == strlen(payload));

    TEST_ASSERT(vfs_link(src_path, link_path) == 0);

    vfs_inode_t *dst = vfs_resolve_path(link_path);
    TEST_ASSERT(dst != nullptr);

    char buf[32] = {0};
    TEST_ASSERT(vfs_read(dst, 0, strlen(payload), (uint8_t *)buf) == strlen(payload));
    TEST_ASSERT(strncmp(buf, payload, strlen(payload)) == 0);

    // Removing the link should not delete the original.
    TEST_ASSERT(vfs_unlink(link_path) == 0);
    TEST_ASSERT(vfs_resolve_path(link_path) == nullptr);

    vfs_inode_t *src_check = vfs_resolve_path(src_path);
    TEST_ASSERT(src_check != nullptr);
    vfs_release(src_check);

    TEST_ASSERT(vfs_unlink(src_path) == 0);

    vfs_release(dst);
    vfs_release(src);
    return true;
}
