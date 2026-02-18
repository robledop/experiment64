#include <fs/pty.h>

#include <drivers/terminal.h>
#include <lib/string.h>
#include <mem/heap.h>
#include <sys/ioctl.h>
#include <task/process.h>
#include <task/signal.h>

#define PTY_BUF_SIZE 4096

typedef struct
{
    uint8_t data[PTY_BUF_SIZE];
    uint32_t read_pos;
    uint32_t write_pos;
    uint32_t count;
} pty_ring_t;

typedef struct
{
    spinlock_t lock;
    pty_ring_t master_to_slave;
    pty_ring_t slave_to_master;
    int master_open;
    int slave_open;
    struct winsize winsz;
    int fg_pid;
} pty_t;

typedef struct
{
    pty_t *pty;
    bool is_master;
} pty_endpoint_t;

static uint64_t pty_inode_read(const vfs_inode_t *node, uint64_t offset, uint64_t size, uint8_t *buffer);
static uint64_t pty_inode_write(vfs_inode_t *node, uint64_t offset, uint64_t size, uint8_t *buffer);
static void pty_inode_close(vfs_inode_t *node);
static int pty_inode_ioctl(vfs_inode_t *node, int request, void *arg);
static vfs_inode_t *pty_inode_clone(const vfs_inode_t *node);

static struct inode_operations pty_inode_ops = {
    .read = pty_inode_read,
    .write = pty_inode_write,
    .truncate = nullptr,
    .open = nullptr,
    .close = pty_inode_close,
    .ioctl = pty_inode_ioctl,
    .readdir = nullptr,
    .finddir = nullptr,
    .clone = pty_inode_clone,
    .mknod = nullptr,
    .link = nullptr,
    .unlink = nullptr,
};

static inline pty_ring_t *pty_rx_ring(const pty_endpoint_t *ep)
{
    return ep->is_master ? &ep->pty->slave_to_master : &ep->pty->master_to_slave;
}

static inline pty_ring_t *pty_tx_ring(const pty_endpoint_t *ep)
{
    return ep->is_master ? &ep->pty->master_to_slave : &ep->pty->slave_to_master;
}

static inline int pty_peer_open_locked(const pty_endpoint_t *ep)
{
    return ep->is_master ? ep->pty->slave_open : ep->pty->master_open;
}

static inline bool pty_ring_push(pty_ring_t *ring, uint8_t value)
{
    if (ring->count >= PTY_BUF_SIZE)
        return false;
    ring->data[ring->write_pos] = value;
    ring->write_pos = (ring->write_pos + 1) % PTY_BUF_SIZE;
    ring->count++;
    return true;
}

static inline bool pty_ring_pop(pty_ring_t *ring, uint8_t *out)
{
    if (ring->count == 0)
        return false;
    *out = ring->data[ring->read_pos];
    ring->read_pos = (ring->read_pos + 1) % PTY_BUF_SIZE;
    ring->count--;
    return true;
}

static vfs_inode_t *pty_create_endpoint_inode(pty_t *pty, bool is_master)
{
    vfs_inode_t *inode = kmalloc(sizeof(vfs_inode_t));
    if (!inode)
        return nullptr;

    pty_endpoint_t *ep = kmalloc(sizeof(pty_endpoint_t));
    if (!ep) {
        kfree(inode);
        return nullptr;
    }

    memset(inode, 0, sizeof(vfs_inode_t));
    ep->pty = pty;
    ep->is_master = is_master;

    inode->flags = VFS_CHARDEVICE;
    inode->ref = 1;
    inode->iops = &pty_inode_ops;
    inode->device = ep;

    spinlock_acquire(&pty->lock);
    if (is_master)
        pty->master_open++;
    else
        pty->slave_open++;
    spinlock_release(&pty->lock);

    return inode;
}

int pty_alloc(vfs_inode_t **master_inode, vfs_inode_t **slave_inode)
{
    if (!master_inode || !slave_inode)
        return -1;

    *master_inode = nullptr;
    *slave_inode = nullptr;

    pty_t *pty = kmalloc(sizeof(pty_t));
    if (!pty)
        return -1;

    memset(pty, 0, sizeof(*pty));
    spinlock_init(&pty->lock);

    int cols = 80;
    int rows = 25;
    int width = 640;
    int height = 480;
    terminal_get_dimensions(&cols, &rows);
    terminal_get_resolution(&width, &height);
    if (cols <= 0)
        cols = 80;
    if (rows <= 0)
        rows = 25;
    if (width <= 0)
        width = 640;
    if (height <= 0)
        height = 480;
    pty->winsz = (struct winsize){
        .ws_row = (uint16_t)rows,
        .ws_col = (uint16_t)cols,
        .ws_xpixel = (uint16_t)width,
        .ws_ypixel = (uint16_t)height,
    };

    vfs_inode_t *master = pty_create_endpoint_inode(pty, true);
    if (!master) {
        kfree(pty);
        return -1;
    }

    vfs_inode_t *slave = pty_create_endpoint_inode(pty, false);
    if (!slave) {
        pty_inode_close(master);
        kfree(master);
        return -1;
    }

    *master_inode = master;
    *slave_inode = slave;
    return 0;
}

static uint64_t pty_inode_read(const vfs_inode_t *node, uint64_t offset, uint64_t size, uint8_t *buffer)
{
    (void)offset;
    if (!node || !node->device || !buffer || size == 0)
        return 0;

    const pty_endpoint_t *ep = (const pty_endpoint_t *)node->device;
    pty_t *pty = ep->pty;
    if (!pty)
        return 0;

    uint64_t bytes_read = 0;
    while (bytes_read < size) {
        spinlock_acquire(&pty->lock);
        pty_ring_t *rx = pty_rx_ring(ep);
        int peer_open = pty_peer_open_locked(ep);

        while (bytes_read < size && rx->count > 0) {
            uint8_t c = 0;
            if (!pty_ring_pop(rx, &c))
                break;
            buffer[bytes_read++] = c;
        }

        spinlock_release(&pty->lock);

        if (bytes_read > 0)
            break;
        if (peer_open == 0)
            break;

        schedule();
    }

    return bytes_read;
}

// NOLINTNEXTLINE(readability-non-const-parameter) - Must match inode_operations signature
static uint64_t pty_inode_write(vfs_inode_t *node, uint64_t offset, uint64_t size, uint8_t *buffer)
{
    (void)offset;
    if (!node || !node->device || !buffer || size == 0)
        return 0;

    const pty_endpoint_t *ep = (const pty_endpoint_t *)node->device;
    pty_t *pty = ep->pty;
    if (!pty)
        return 0;

    uint64_t bytes_written = 0;
    while (bytes_written < size) {
        bool ring_full = false;
        int peer_open = 0;
        int sigint_pid = 0;
        int sigint_count = 0;

        spinlock_acquire(&pty->lock);
        pty_ring_t *tx = pty_tx_ring(ep);
        peer_open = pty_peer_open_locked(ep);
        if (peer_open == 0) {
            spinlock_release(&pty->lock);
            break;
        }

        while (bytes_written < size && tx->count < PTY_BUF_SIZE) {
            const uint8_t c = buffer[bytes_written++];
            if (ep->is_master && c == 0x03) {
                if (pty->fg_pid > 0) {
                    sigint_pid = pty->fg_pid;
                    sigint_count++;
                }
                continue;
            }

            if (!pty_ring_push(tx, c))
                break;
        }

        ring_full = (tx->count >= PTY_BUF_SIZE);
        spinlock_release(&pty->lock);

        for (int i = 0; i < sigint_count; i++) {
            signal_send_pid(sigint_pid, SIGINT);
        }

        if (bytes_written == size)
            break;
        if (peer_open == 0)
            break;
        if (ring_full)
            schedule();
    }

    return bytes_written;
}

static void pty_inode_close(vfs_inode_t *node)
{
    if (!node || !node->device)
        return;

    pty_endpoint_t *ep = (pty_endpoint_t *)node->device;
    pty_t *pty = ep->pty;
    if (!pty) {
        kfree(ep);
        return;
    }

    bool free_pty = false;
    spinlock_acquire(&pty->lock);
    if (ep->is_master) {
        if (pty->master_open > 0)
            pty->master_open--;
    } else {
        if (pty->slave_open > 0)
            pty->slave_open--;
    }
    if (pty->master_open == 0 && pty->slave_open == 0)
        free_pty = true;
    spinlock_release(&pty->lock);

    kfree(ep);
    if (free_pty)
        kfree(pty);
}

static int pty_inode_ioctl(vfs_inode_t *node, int request, void *arg)
{
    if (!node || !node->device)
        return -1;

    const pty_endpoint_t *ep = (const pty_endpoint_t *)node->device;
    pty_t *pty = ep->pty;
    if (!pty)
        return -1;

    switch (request) {
    case TIOCGWINSZ:
    {
        if (!arg)
            return -1;
        struct winsize ws = {0};
        spinlock_acquire(&pty->lock);
        ws = pty->winsz;
        spinlock_release(&pty->lock);
        memcpy(arg, &ws, sizeof(ws));
        return 0;
    }
    case TIOCSWINSZ:
    {
        if (!arg)
            return -1;
        struct winsize ws = {0};
        memcpy(&ws, arg, sizeof(ws));
        spinlock_acquire(&pty->lock);
        pty->winsz = ws;
        spinlock_release(&pty->lock);
        return 0;
    }
    case TIOCSPGRP:
    {
        if (!arg || ep->is_master)
            return -1;
        int pid = *(int *)arg;
        if (pid < 0)
            pid = 0;
        spinlock_acquire(&pty->lock);
        pty->fg_pid = pid;
        spinlock_release(&pty->lock);
        return 0;
    }
    default:
        return -1;
    }
}

static vfs_inode_t *pty_inode_clone(const vfs_inode_t *node)
{
    if (!node || !node->device)
        return nullptr;

    const pty_endpoint_t *old_ep = (const pty_endpoint_t *)node->device;
    pty_t *pty = old_ep->pty;
    if (!pty)
        return nullptr;

    vfs_inode_t *copy = kmalloc(sizeof(vfs_inode_t));
    if (!copy)
        return nullptr;

    pty_endpoint_t *new_ep = kmalloc(sizeof(pty_endpoint_t));
    if (!new_ep) {
        kfree(copy);
        return nullptr;
    }

    memset(copy, 0, sizeof(vfs_inode_t));
    new_ep->pty = pty;
    new_ep->is_master = old_ep->is_master;

    copy->flags = node->flags;
    copy->ref = 1;
    copy->iops = node->iops;
    copy->device = new_ep;

    spinlock_acquire(&pty->lock);
    if (new_ep->is_master)
        pty->master_open++;
    else
        pty->slave_open++;
    spinlock_release(&pty->lock);

    return copy;
}
