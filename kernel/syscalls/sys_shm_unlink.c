#include <ipc/shm.h>
#include <status.h>
#include <syscall_common.h>

/**
 * Unlink a shared memory object with the specified name. This will mark the shared memory object for deletion, and it
 * will be destroyed once all processes that have it open have closed it. If no shared memory object with the specified
 * name exists, an error will be returned.
 * @param name The name of the shared memory object to unlink. Must be a null-terminated string with a maximum length of
 * SHM_NAME_MAX - 1 characters.
 * @return 0 on success, or a negative error code on failure.
 */
int sys_shm_unlink(const char *name)
{
    if (!name)
        return -EINVAL;

    char kname[SHM_NAME_MAX];
    int status = copy_from_user_str_checked(kname, name, SHM_NAME_MAX, "sys_shm_unlink name", -EFAULT);
    if (status != 0)
        return status;

    return shm_do_unlink(kname);
}
