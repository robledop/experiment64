#include "test.h"
#include "vfs.h"
#include "string.h"
#include "heap.h"

static char mock_file_content[128] = "Hello VFS";
static int mock_open_count = 0;
static int mock_close_count = 0;

// Mock FS implementation
static uint64_t mock_read(vfs_inode_t *node, uint64_t offset, uint64_t size, uint8_t *buffer)
{
    (void)node;
    uint64_t len = strlen(mock_file_content);
    if (offset >= len)
        return 0;
    if (offset + size > len)
        size = len - offset;
    memcpy(buffer, mock_file_content + offset, size);
    return size;
}

static uint64_t mock_write(vfs_inode_t *node, uint64_t offset, uint64_t size, uint8_t *buffer)
{
    (void)node;
    if (offset + size >= sizeof(mock_file_content))
        return 0;
    memcpy(mock_file_content + offset, buffer, size);
    // Ensure null termination for string operations in test
    if (offset + size < sizeof(mock_file_content))
        mock_file_content[offset + size] = 0;
    return size;
}

static void mock_open(vfs_inode_t *node)
{
    (void)node;
    mock_open_count++;
}

static void mock_close(vfs_inode_t *node)
{
    (void)node;
    mock_close_count++;
}

static vfs_inode_t *mock_finddir(vfs_inode_t *node, char *name)
{
    (void)node;
    if (strcmp(name, "testfile") == 0)
    {
        vfs_inode_t *file = kmalloc(sizeof(vfs_inode_t));
        memset(file, 0, sizeof(vfs_inode_t));
        file->flags = VFS_FILE;
        file->read = mock_read;
        file->write = mock_write;
        file->open = mock_open;
        file->close = mock_close;
        return file;
    }
    return 0;
}

static void setup_mock_fs(void)
{
    strcpy(mock_file_content, "Hello VFS");
    mock_open_count = 0;
    mock_close_count = 0;

    // Setup a mock root
    // We use a static variable to ensure it persists if needed,
    // though for these tests local would suffice if we are careful.
    static vfs_inode_t root;
    memset(&root, 0, sizeof(vfs_inode_t));
    root.flags = VFS_DIRECTORY;
    root.finddir = mock_finddir;

    vfs_root = &root;
}

TEST(test_vfs_open)
{
    setup_mock_fs();

    vfs_inode_t *file = vfs_finddir(vfs_root, "testfile");
    if (!file)
        return false;

    vfs_open(file);
    bool passed = (mock_open_count == 1);

    kfree(file);
    return passed;
}

TEST(test_vfs_write)
{
    setup_mock_fs();

    vfs_inode_t *file = vfs_finddir(vfs_root, "testfile");
    if (!file)
        return false;

    const char *new_data = "WriteTest";
    uint64_t written = vfs_write(file, 0, strlen(new_data), (uint8_t *)new_data);

    bool passed = (written == strlen(new_data));
    if (passed)
    {
        // Verify content
        if (strcmp(mock_file_content, "WriteTest") != 0)
            passed = false;
    }

    kfree(file);
    return passed;
}

TEST(test_vfs_read)
{
    setup_mock_fs();

    vfs_inode_t *file = vfs_finddir(vfs_root, "testfile");
    if (!file)
        return false;

    char buffer[32];
    memset(buffer, 0, 32);
    uint64_t bytes = vfs_read(file, 0, 32, (uint8_t *)buffer);

    bool passed = (bytes > 0 && strcmp(buffer, "Hello VFS") == 0);

    kfree(file);
    return passed;
}

TEST(test_vfs_close)
{
    setup_mock_fs();

    vfs_inode_t *file = vfs_finddir(vfs_root, "testfile");
    if (!file)
        return false;

    vfs_open(file);
    vfs_close(file);

    bool passed = (mock_close_count == 1);

    kfree(file);
    return passed;
}

TEST(test_vfs_basic)
{
    setup_mock_fs();

    // Test finddir
    vfs_inode_t *file = vfs_finddir(vfs_root, "testfile");
    if (!file)
    {
        printf("VFS: Failed to find 'testfile'\n");
        return false;
    }

    // Test open
    vfs_open(file);
    if (mock_open_count != 1)
    {
        printf("VFS: Open count mismatch\n");
        return false;
    }

    // Test read
    char buffer[32];
    memset(buffer, 0, 32);
    uint64_t bytes = vfs_read(file, 0, 32, (uint8_t *)buffer);

    if (bytes == 0)
    {
        printf("VFS: Read returned 0 bytes\n");
        return false;
    }

    if (strcmp(buffer, "Hello VFS") != 0)
    {
        printf("VFS: Read wrong data: '%s'\n", buffer);
        return false;
    }

    // Test write
    const char *new_data = "New Data";
    uint64_t written = vfs_write(file, 0, strlen(new_data), (uint8_t *)new_data);
    if (written != strlen(new_data))
    {
        printf("VFS: Write failed\n");
        return false;
    }

    // Verify write with read
    memset(buffer, 0, 32);
    vfs_read(file, 0, 32, (uint8_t *)buffer);
    if (strncmp(buffer, new_data, strlen(new_data)) != 0)
    {
        printf("VFS: Read back wrong data after write: '%s'\n", buffer);
        return false;
    }

    // Test close
    vfs_close(file);
    if (mock_close_count != 1)
    {
        printf("VFS: Close count mismatch\n");
        return false;
    }

    printf("VFS: Basic test passed. Read: %s\n", buffer);
    kfree(file);
    return true;
}
