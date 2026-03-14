#include <syscall_common.h>
#include <fs/pipe.h>
#include <mem/heap.h>
#include <sys/fcntl.h>

int sys_pipe(int pipefd[2])
{
    if (!pipefd)
        return -1;
    if (!user_ptr_write_ok(pipefd, sizeof(int) * 2, "sys_pipe"))
        return -1;

    vfs_inode_t *read_inode  = nullptr;
    vfs_inode_t *write_inode = nullptr;
    if (pipe_alloc(&read_inode, &write_inode) != 0)
        return -1;

    file_descriptor_t *read_desc = fd_alloc(read_inode, O_RDONLY);
    if (!read_desc) {
        kfree(read_inode);
        kfree(write_inode);
        return -1;
    }

    file_descriptor_t *write_desc = fd_alloc(write_inode, O_WRONLY);
    if (!write_desc) {
        kfree(read_desc);
        kfree(read_inode);
        kfree(write_inode);
        return -1;
    }
    int read_fd        = -1;
    int write_fd       = -1;
    uint64_t flags;
    SPIN_LOCK_INT_SAVE(current_process->fd_lock, flags);
    for (int i = 3; i < MAX_FDS && (read_fd == -1 || write_fd == -1); i++) {
        if (current_process->fd_table[i] == nullptr) {
            if (read_fd == -1)
                read_fd = i;
            else
                write_fd = i;
        }
    }
    if (read_fd != -1 && write_fd != -1) {
        current_process->fd_table[read_fd]  = read_desc;
        current_process->fd_table[write_fd] = write_desc;
    }
    SPIN_UNLOCK_INT_RESTORE(current_process->fd_lock, flags);
    if (read_fd == -1 || write_fd == -1) {
        kfree(write_desc);
        kfree(read_desc);
        kfree(read_inode->device); // Free the shared pipe_t
        kfree(read_inode);
        kfree(write_inode);
        return -1;
    }

    pipefd[0] = read_fd;
    pipefd[1] = write_fd;

    return 0;
}