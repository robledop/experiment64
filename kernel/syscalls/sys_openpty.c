#include <syscall_common.h>

#include <fs/pty.h>
#include <mem/heap.h>
#include <status.h>
#include <sys/fcntl.h>
#include <sys/syscall.h>

static file_descriptor_t *pty_fd_new(vfs_inode_t *inode)
{
    if (!inode)
        return nullptr;

    file_descriptor_t *desc = kmalloc(sizeof(file_descriptor_t));
    if (!desc)
        return nullptr;

    memset(desc, 0, sizeof(*desc));
    desc->inode = inode;
    desc->offset = 0;
    desc->flags = O_RDWR;
    desc->ref = 1;
    return desc;
}

/**
 * Create a new pseudo-terminal pair and return their file descriptors
 * @param fds An array of two integers where the master and slave file descriptors will be stored
 * @return 0 on success, -1 on failure
 */
int sys_openpty(int fds[2])
{
    if (!fds)
        return -1;
    if (!user_ptr_write_ok(fds, sizeof(int) * 2, "sys_openpty"))
        return -1;

    vfs_inode_t *master_inode = nullptr;
    vfs_inode_t *slave_inode = nullptr;
    if (pty_alloc(&master_inode, &slave_inode) != 0)
        return -1;

    file_descriptor_t *master_desc = pty_fd_new(master_inode);
    if (!master_desc) {
        vfs_release(master_inode);
        vfs_release(slave_inode);
        return -1;
    }

    file_descriptor_t *slave_desc = pty_fd_new(slave_inode);
    if (!slave_desc) {
        fd_put(master_desc);
        vfs_release(slave_inode);
        return -1;
    }

    int master_fd = -1;
    int slave_fd = -1;
    uint64_t flags = 0;
    SPIN_LOCK_INT_SAVE(current_process->fd_lock, flags);
    for (int i = 0; i < MAX_FDS; i++) {
        if (current_process->fd_table[i] != nullptr)
            continue;
        if (master_fd < 0) {
            master_fd = i;
            continue;
        }
        slave_fd = i;
        break;
    }

    if (master_fd >= 0 && slave_fd >= 0) {
        current_process->fd_table[master_fd] = master_desc;
        current_process->fd_table[slave_fd] = slave_desc;
    }
    SPIN_UNLOCK_INT_RESTORE(current_process->fd_lock, flags);

    if (master_fd < 0 || slave_fd < 0) {
        fd_put(master_desc);
        fd_put(slave_desc);
        return -1;
    }

    int fds_out[2] = { master_fd, slave_fd };
    if (!copy_to_user(fds, fds_out, sizeof(fds_out))) {
        sys_close(master_fd);
        sys_close(slave_fd);
        return -EFAULT;
    }
    return 0;
}
