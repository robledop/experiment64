#include "syscall_common.h"

int sys_link(const char* oldpath, const char* newpath)
{
    if (!oldpath || !newpath || !*oldpath || !*newpath)
        return -1;

    char abs_old[PATH_MAX];
    char abs_new[PATH_MAX];
    resolve_user_path(oldpath, abs_old, sizeof(abs_old));
    resolve_user_path(newpath, abs_new, sizeof(abs_new));

    return vfs_link(abs_old, abs_new);
}
