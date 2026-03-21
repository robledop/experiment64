#include <fs/pty.h>

#include <drivers/terminal.h>
#include <drivers/tsc.h>
#include <lib/string.h>
#include <mem/heap.h>
#include <sys/ioctl.h>
#include <sys/poll.h>
#include <sys/termios.h>
#include <syscall_common.h>
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
    int revoked;
    struct winsize winsz;
    int fg_pid;
} pty_t;

typedef struct
{
    pty_t *pty;
    bool is_master;
    struct termios termios;
    int nonblock_read;
} pty_endpoint_t;

static uint64_t pty_inode_read(const vfs_inode_t *node, uint64_t offset, uint64_t size, uint8_t *buffer);
static uint64_t pty_inode_write(vfs_inode_t *node, uint64_t offset, uint64_t size, uint8_t *buffer);
static void pty_inode_close(vfs_inode_t *node);
static int pty_inode_ioctl(vfs_inode_t *node, int request, void *arg);
static int pty_inode_poll(const vfs_inode_t *node, short events, short *revents);
static vfs_inode_t *pty_inode_clone(const vfs_inode_t *node);

static struct inode_operations pty_inode_ops = {
    .read = pty_inode_read,
    .write = pty_inode_write,
    .truncate = nullptr,
    .open = nullptr,
    .close = pty_inode_close,
    .ioctl = pty_inode_ioctl,
    .poll = pty_inode_poll,
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

static struct termios pty_default_termios(void)
{
    return (struct termios){
        .c_iflag = IXON | ICRNL,
        .c_oflag = OPOST,
        .c_cflag = 0,
        .c_lflag = ECHO | ICANON,
        .c_cc = {[VMIN] = 1, [VTIME] = 0},
    };
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
    ep->termios = pty_default_termios();
    ep->nonblock_read = 0;

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

    pty_endpoint_t *ep = (pty_endpoint_t *)node->device;
    pty_t *pty = ep->pty;
    if (!pty)
        return 0;

    const uint32_t vmin = ep->termios.c_cc[VMIN] > size ? (uint32_t)size : ep->termios.c_cc[VMIN];
    const uint32_t vtime = ep->termios.c_cc[VTIME];
    const int nonblock = ep->nonblock_read;
    const uint64_t timeout_ns = (uint64_t)vtime * 100000000ull;

    uint64_t bytes_read = 0;
    uint64_t deadline_ns = 0;
    bool deadline_active = false;

    while (bytes_read < size) {
        spinlock_acquire(&pty->lock);
        pty_ring_t *rx = pty_rx_ring(ep);
        int peer_open = pty_peer_open_locked(ep);
        int revoked = pty->revoked;

        while (bytes_read < size && rx->count > 0) {
            uint8_t c = 0;
            if (!pty_ring_pop(rx, &c))
                break;
            buffer[bytes_read++] = c;
        }

        spinlock_release(&pty->lock);

        if (bytes_read > 0 && vmin == 0)
            break;
        if (bytes_read >= vmin && vmin != 0)
            break;

        if (bytes_read > 0 && vtime > 0) {
            if (!deadline_active) {
                deadline_ns = tsc_monotonic_ns() + timeout_ns;
                deadline_active = true;
            }
        }

        if (peer_open == 0 || revoked)
            break;

        if (nonblock)
            break;

        if (bytes_read == 0 && vmin == 0) {
            if (vtime == 0) {
                break;
            }
            if (!deadline_active) {
                deadline_ns = tsc_monotonic_ns() + timeout_ns;
                deadline_active = true;
            } else if (tsc_monotonic_ns() >= deadline_ns) {
                break;
            }
        } else if (bytes_read > 0 && vtime > 0 && deadline_active && tsc_monotonic_ns() >= deadline_ns) {
            break;
        }

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

    pty_endpoint_t *ep = (pty_endpoint_t *)node->device;
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
        if (peer_open == 0 || pty->revoked) {
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
    int hangup_pid = 0;
    spinlock_acquire(&pty->lock);
    if (ep->is_master) {
        if (pty->master_open > 0)
            pty->master_open--;
        if (pty->master_open == 0) {
            pty->revoked = 1;
            if (pty->slave_open > 0 && pty->fg_pid > 1) {
                hangup_pid = pty->fg_pid;
                pty->fg_pid = 0;
            }
        }
    } else {
        if (pty->slave_open > 0)
            pty->slave_open--;
    }
    if (pty->master_open == 0 && pty->slave_open == 0)
        free_pty = true;
    spinlock_release(&pty->lock);

    if (hangup_pid > 0)
        signal_send_pid(hangup_pid, SIGHUP);

    kfree(ep);
    if (free_pty)
        kfree(pty);
}

static int pty_inode_ioctl(vfs_inode_t *node, int request, void *arg)
{
    if (!node || !node->device)
        return -1;

    pty_endpoint_t *ep = (pty_endpoint_t *)node->device;
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
        copy_to_user(arg, &ws, sizeof(ws));
        return 0;
    }
    case TIOCSWINSZ:
    {
        if (!arg)
            return -1;
        struct winsize ws = {0};
        copy_from_user(&ws, arg, sizeof(ws));
        int winch_pid = 0;
        spinlock_acquire(&pty->lock);
        const bool changed = ws.ws_row != pty->winsz.ws_row || ws.ws_col != pty->winsz.ws_col;
        pty->winsz         = ws;
        if (changed && pty->fg_pid > 0)
            winch_pid = pty->fg_pid;
        spinlock_release(&pty->lock);
        if (winch_pid > 0)
            signal_send_pid(winch_pid, SIGWINCH);
        return 0;
    }
    case TIOCGETA:
    {
        if (!arg)
            return -1;
        copy_to_user(arg, &ep->termios, sizeof(ep->termios));
        return 0;
    }
    case TIOCSETA:
    case TCSETSW:
    case TCSETSF:
    {
        if (!arg)
            return -1;
        struct termios t = {0};
        copy_from_user(&t, arg, sizeof(t));
        ep->termios = t;
        return 0;
    }
    // TIOCGPGRP works on both master and slave (read-only query),
    // while TIOCSPGRP is slave-only to prevent the master from
    // setting fg_pid (matches POSIX convention).
    case TIOCGPGRP:
    {
        if (!arg)
            return -1;
        int pid = 0;
        spinlock_acquire(&pty->lock);
        pid = pty->fg_pid;
        spinlock_release(&pty->lock);
        copy_to_user(arg, &pid, sizeof(pid));
        return 0;
    }
    case TIOCSPGRP:
    {
        if (!arg || ep->is_master)
            return -1;
        int pid = 0;
        copy_from_user(&pid, arg, sizeof(pid));
        if (pid < 0)
            pid = 0;
        spinlock_acquire(&pty->lock);
        pty->fg_pid = pid;
        spinlock_release(&pty->lock);
        return 0;
    }
    case TIOCHUP:
    {
        spinlock_acquire(&pty->lock);
        pty->revoked = 1;
        spinlock_release(&pty->lock);
        return 0;
    }
    case FIONBIO:
    {
        if (!arg)
            return -1;
        int val = 0;
        copy_from_user(&val, arg, sizeof(val));
        ep->nonblock_read = val ? 1 : 0;
        return 0;
    }
    default:
        return -1;
    }
}

static int pty_inode_poll(const vfs_inode_t *node, short events, short *revents)
{
    if (!node || !node->device || !revents)
        return -1;

    const pty_endpoint_t *ep = (const pty_endpoint_t *)node->device;
    pty_t *pty               = ep->pty;
    if (!pty)
        return -1;

    short out = 0;
    spinlock_acquire(&pty->lock);
    pty_ring_t *rx   = pty_rx_ring(ep);
    pty_ring_t *tx   = pty_tx_ring(ep);
    int peer_open    = pty_peer_open_locked(ep);
    int revoked      = pty->revoked;

    if ((events & (POLLIN | POLLPRI)) && rx->count > 0)
        out |= POLLIN;
    if ((events & POLLOUT) && peer_open > 0 && tx->count < PTY_BUF_SIZE && !revoked)
        out |= POLLOUT;
    if (peer_open == 0 || revoked)
        out |= POLLHUP;

    spinlock_release(&pty->lock);

    *revents = out;
    return 0;
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
    new_ep->termios = old_ep->termios;
    new_ep->nonblock_read = old_ep->nonblock_read;

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
