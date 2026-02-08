#include <syscall_common.h>
#include <drivers/keyboard.h>
#include <lib/util.h>

int sys_read(int fd, char *buf, size_t count)
{
    if (fd < 0 || fd >= MAX_FDS)
        return 0;
    if (count == 0)
        return 0;
    if (!user_ptr_write_ok(buf, count, "sys_read"))
        return -1;

    file_descriptor_t *desc = fd_get(fd);

    // Handle stdin (fd 0) especially only if it's the console device or not set up
    if (fd == 0) {
        // If fd 0 has a real descriptor with an inode, use it
        if (desc && desc->inode) {
            if (!fd_can_read(desc)) {
                fd_put(desc);
                return -1;
            }
            uint64_t read = vfs_read(desc->inode, desc->offset, count, (uint8_t *)buf);
            desc->offset  += read;
            fd_put(desc);
            return clamp_to_int(read);
        }

        // No descriptor or no inode - fall back to keyboard
        if (desc && !fd_can_read(desc)) {
            fd_put(desc);
            return -1;
        }

        size_t read = 0;
        while (read < count) {
            if (read > 0 && !keyboard_has_char()) {
                break;
            }

            char c = keyboard_get_char();
            if (c) {
                buf[read++] = c;
            }
        }
        if (read == 0 && !keyboard_has_char())
            keyboard_clear_modifiers();
        if (desc)
            fd_put(desc);
        return clamp_to_int(read);
    }

    if (!desc)
        return 0;
    if (!desc->inode) {
        fd_put(desc);
        return 0;
    }
    if (!fd_can_read(desc)) {
        fd_put(desc);
        return -1;
    }

    uint64_t read = vfs_read(desc->inode, desc->offset, count, (uint8_t *)buf);
    desc->offset  += read;
    fd_put(desc);
    return clamp_to_int(read);
}