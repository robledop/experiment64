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
    if (!user_ptr_read_ok(name, 1, "sys_shm_open name"))
        return -EFAULT;

    char kname[SHM_NAME_MAX];
    if (!copy_from_user_str(kname, name, SHM_NAME_MAX))
        return -EFAULT;
    if (kname[0] == '\0')
        return -EINVAL;

    bool create = (flags & O_CREATE) != 0;

    shm_entry_t *entry = shm_lookup(kname);

    if (entry && create)
        return -EINSTKN;

    if (!entry && !create)
        return -ENOENT;

    if (!entry) {
        if (size == 0)
            return -EINVAL;
        entry = shm_create(kname, size);
        if (!entry)
            return -ENOMEM;
    }

    vfs_inode_t *inode = kmalloc(sizeof(vfs_inode_t));
    if (!inode)
        return -ENOMEM;

    memset(inode, 0, sizeof(vfs_inode_t));
    inode->flags  = VFS_CHARDEVICE;
    inode->iops   = &shm_inode_ops;
    inode->device = entry;
    inode->size   = entry->size;
    inode->ref    = 1;

    file_descriptor_t *desc = kmalloc(sizeof(file_descriptor_t));
    if (!desc) {
        kfree(inode);
        return -ENOMEM;
    }

    desc->inode  = inode;
    desc->offset = 0;
    desc->flags  = O_RDWR;
    desc->ref    = 1;

    int fd = fd_assign(desc, 0);
    if (fd < 0) {
        kfree(inode);
        kfree(desc);
        return -ENOMEM;
    }

    shm_ref(entry);
    return fd;
}