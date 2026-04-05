#include <syscall_common.h>
#include <lib/util.h>
#include <status.h>

int sys_read(int fd, char *buf, size_t count)
{
    if (fd < 0 || fd >= MAX_FDS)
        return -EBADF;
    if (count == 0)
        return 0;
    if (!user_ptr_write_ok(buf, count, "sys_read"))
        return -EFAULT;

    file_descriptor_t *desc = fd_get(fd);
    if (!desc)
        return -EBADF;
    if (!desc->inode) {
        fd_put(desc);
        return -EBADF;
    }
    if (!fd_can_read(desc)) {
        fd_put(desc);
        return -EBADF;
    }

    uint64_t read = vfs_read(desc->inode, desc->offset, count, (uint8_t *)buf);
    desc->offset  += read;
    fd_put(desc);
    return clamp_to_int(read);
}
