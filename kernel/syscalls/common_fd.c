#include <mem/heap.h>
#include <sys/fcntl.h>
#include <syscall_common.h>

file_descriptor_t *fd_alloc(vfs_inode_t *inode, int flags)
{
    file_descriptor_t *desc = kmalloc(sizeof(file_descriptor_t));
    if (!desc)
        return nullptr;

    memset(desc, 0, sizeof(file_descriptor_t));
    desc->inode = inode;
    desc->flags = flags;
    desc->ref   = 1;
    return desc;
}

/**
 * @brief Check whether a file descriptor allows reading.
 *
 * Reading is blocked only when the descriptor was opened O_WRONLY.
 * O_RDONLY (== 0) and O_RDWR both permit reading.
 */
bool fd_can_read(const file_descriptor_t *desc)
{
    if (!desc)
        return false;
    return (desc->flags & (O_WRONLY | O_RDWR)) != O_WRONLY;
}

/**
 * @brief Check whether a file descriptor allows writing.
 *
 * Writing is permitted when either O_WRONLY or O_RDWR is set in the flags.
 */
bool fd_can_write(const file_descriptor_t *desc)
{
    if (!desc)
        return false;
    return (desc->flags & (O_WRONLY | O_RDWR)) != 0;
}

file_descriptor_t *fd_get(int fd)
{
    if (!current_process)
        return nullptr;
    if (fd < 0 || fd >= MAX_FDS)
        return nullptr;

    file_descriptor_t *desc = nullptr;
    WITH_LOCK(current_process->fd_lock) {
        desc = current_process->fd_table[fd];
        if (desc)
            __atomic_add_fetch(&desc->ref, 1, __ATOMIC_RELAXED);
    }
    return desc;
}

void fd_put(file_descriptor_t *desc)
{
    if (!desc)
        return;

    uint32_t ref = __atomic_sub_fetch(&desc->ref, 1, __ATOMIC_RELEASE);
    if (ref != 0)
        return;

    __atomic_thread_fence(__ATOMIC_ACQUIRE);
    vfs_release(desc->inode);
    kfree(desc);
}

int fd_assign(file_descriptor_t *desc, int start_fd)
{
    if (!current_process || !desc)
        return -1;
    if (start_fd < 0)
        start_fd = 0;

    int fd = -1;
    WITH_LOCK(current_process->fd_lock) {
        for (int i = start_fd; i < MAX_FDS; i++) {
            if (current_process->fd_table[i] == nullptr) {
                current_process->fd_table[i] = desc;
                fd                           = i;
                break;
            }
        }
    }
    return fd;
}
