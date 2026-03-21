#include <ipc/shm.h>
#include <mem/heap.h>
#include <status.h>
#include <sys/fcntl.h>
#include <syscall_common.h>

/**
 * Create or open a shared memory object with the specified name and size. If the O_CREATE flag is set, a new shared
 * memory object will be created if one does not already exist. If the O_CREATE flag is not set, an existing shared
 * memory object with the specified name must already exist. The size parameter is only used when creating a new shared
 * memory object and is ignored when opening an existing one.
 * @param name The name of the shared memory object to create or open. Must be a null-terminated string with a maximum
 * length of SHM_NAME_MAX - 1 characters.
 * @param flags The flags for opening the shared memory object. Can be O_CREATE to create a new object if it does not
 * exist.
 * @param size The size of the shared memory object to create. Ignored if opening an existing object.
 * @return The file descriptor for the shared memory object on success, or a negative error code on failure.
 */
int sys_shm_open(const char *name, int flags, size_t size)
{
    if (!name)
        return -EINVAL;

    char kname[SHM_NAME_MAX];
    int status = copy_from_user_str_checked(kname, name, SHM_NAME_MAX, "sys_shm_open name", -EFAULT);
    if (status != 0)
        return status;
    if (kname[0] == '\0')
        return -EINVAL;

    shm_entry_t *entry = shm_open_or_create(kname, flags, size);
    if (!entry)
        return (flags & O_CREATE) ? -ENOMEM : -ENOENT;

    vfs_inode_t *inode = kmalloc(sizeof(vfs_inode_t));
    if (!inode) {
        shm_unref(entry);
        return -ENOMEM;
    }

    memset(inode, 0, sizeof(vfs_inode_t));
    inode->flags  = VFS_CHARDEVICE;
    inode->iops   = &shm_inode_ops;
    inode->device = entry;
    inode->size   = entry->size;
    inode->ref    = 1;

    file_descriptor_t *desc = fd_alloc(inode, O_RDWR);
    if (!desc) {
        shm_unref(entry);
        kfree(inode);
        return -ENOMEM;
    }

    int fd = fd_assign(desc, 0);
    if (fd < 0) {
        shm_unref(entry);
        kfree(inode);
        kfree(desc);
        return -ENOMEM;
    }

    return fd;
}
