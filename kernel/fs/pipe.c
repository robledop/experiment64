/**
 * Pipe implementation
 *
 * Provides unidirectional byte stream communication between processes.
 * A pipe has a read end and a write end. Data written to the write end
 * can be read from the read end.
 */

#include "pipe.h"
#include "heap.h"
#include "string.h"
#include "process.h"

// Pipe inode operations
static uint64_t pipe_inode_read(const vfs_inode_t *node, uint64_t offset, uint64_t size, uint8_t *buffer);
// NOLINTNEXTLINE(readability-non-const-parameter) - Must match inode_operations signature
static uint64_t pipe_inode_write(vfs_inode_t *node, uint64_t offset, uint64_t size, uint8_t *buffer);
static void pipe_inode_close(vfs_inode_t *node);

static struct inode_operations pipe_read_ops = {
    .read = pipe_inode_read,
    .write = nullptr,
    .truncate = nullptr,
    .open = nullptr,
    .close = pipe_inode_close,
    .ioctl = nullptr,
    .readdir = nullptr,
    .finddir = nullptr,
    .clone = nullptr,
    .mknod = nullptr,
    .link = nullptr,
    .unlink = nullptr,
};

static struct inode_operations pipe_write_ops = {
    .read = nullptr,
    .write = pipe_inode_write,
    .truncate = nullptr,
    .open = nullptr,
    .close = pipe_inode_close,
    .ioctl = nullptr,
    .readdir = nullptr,
    .finddir = nullptr,
    .clone = nullptr,
    .mknod = nullptr,
    .link = nullptr,
    .unlink = nullptr,
};

int pipe_alloc(vfs_inode_t **read_inode, vfs_inode_t **write_inode)
{
    if (!read_inode || !write_inode)
        return -1;

    // Allocate pipe structure
    pipe_t *p = kmalloc(sizeof(pipe_t));
    if (!p)
        return -1;

    memset(p, 0, sizeof(pipe_t));
    spinlock_init(&p->lock);
    p->read_pos = 0;
    p->write_pos = 0;
    p->count = 0;
    p->read_open = 1;
    p->write_open = 1;

    // Allocate read inode
    vfs_inode_t *ri = kmalloc(sizeof(vfs_inode_t));
    if (!ri)
    {
        kfree(p);
        return -1;
    }

    // Allocate write inode
    vfs_inode_t *wi = kmalloc(sizeof(vfs_inode_t));
    if (!wi)
    {
        kfree(ri);
        kfree(p);
        return -1;
    }

    // Initialize read inode
    memset(ri, 0, sizeof(vfs_inode_t));
    ri->flags = VFS_PIPE;
    ri->inode = 0;
    ri->size = 0;
    ri->ref = 1;
    ri->iops = &pipe_read_ops;
    ri->ptr = nullptr;
    ri->device = p;

    // Initialize write inode
    memset(wi, 0, sizeof(vfs_inode_t));
    wi->flags = VFS_PIPE;
    wi->inode = 0;
    wi->size = 0;
    wi->ref = 1;
    wi->iops = &pipe_write_ops;
    wi->ptr = nullptr;
    wi->device = p;

    *read_inode = ri;
    *write_inode = wi;
    return 0;
}

static uint64_t pipe_inode_read(const vfs_inode_t *node, uint64_t offset, uint64_t size, uint8_t *buffer)
{
    (void)offset; // Pipes ignore offset

    if (!node || !buffer || size == 0)
        return 0;

    pipe_t *p = (pipe_t *)node->device;
    if (!p)
        return 0;

    uint64_t bytes_read = 0;

    spinlock_acquire(&p->lock);

    // Wait for data or closed write end
    while (p->count == 0 && p->write_open > 0)
    {
        spinlock_release(&p->lock);
        schedule(); // Yield to let writer run
        spinlock_acquire(&p->lock);
    }

    // Read available data
    while (bytes_read < size && p->count > 0)
    {
        buffer[bytes_read++] = p->buffer[p->read_pos];
        p->read_pos = (p->read_pos + 1) % PIPE_BUF_SIZE;
        p->count--;
    }

    spinlock_release(&p->lock);
    return bytes_read;
}

// NOLINTNEXTLINE(readability-non-const-parameter) - Must match inode_operations signature
static uint64_t pipe_inode_write(vfs_inode_t *node, uint64_t offset, uint64_t size, uint8_t *buffer)
{
    (void)offset; // Pipes ignore offset

    if (!node || !buffer || size == 0)
        return 0;

    pipe_t *p = (pipe_t *)node->device;
    if (!p)
        return 0;

    uint64_t bytes_written = 0;

    spinlock_acquire(&p->lock);

    // Check if read end is closed
    if (p->read_open == 0)
    {
        spinlock_release(&p->lock);
        return 0; // Broken pipe
    }

    // Write data
    while (bytes_written < size)
    {
        // Wait for space in buffer
        while (p->count >= PIPE_BUF_SIZE && p->read_open > 0)
        {
            spinlock_release(&p->lock);
            schedule(); // Yield to let reader run
            spinlock_acquire(&p->lock);
        }

        // Check again if read end closed while waiting
        if (p->read_open == 0)
            break;

        // Write as much as possible
        while (bytes_written < size && p->count < PIPE_BUF_SIZE)
        {
            p->buffer[p->write_pos] = buffer[bytes_written++];
            p->write_pos = (p->write_pos + 1) % PIPE_BUF_SIZE;
            p->count++;
        }
    }

    spinlock_release(&p->lock);
    return bytes_written;
}

static void pipe_inode_close(vfs_inode_t *node)
{
    if (!node)
        return;

    pipe_t *p = (pipe_t *)node->device;
    if (!p)
        return;

    spinlock_acquire(&p->lock);

    // Determine if this is read or write end based on iops
    if (node->iops == &pipe_read_ops)
    {
        p->read_open--;
    }
    else if (node->iops == &pipe_write_ops)
    {
        p->write_open--;
    }

    // If both ends closed, free the pipe
    bool should_free = (p->read_open <= 0 && p->write_open <= 0);

    spinlock_release(&p->lock);

    if (should_free)
    {
        kfree(p);
    }
}
