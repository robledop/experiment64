#include "syscall_common.h"

#include <lib/string.h>

int sys_unlink(const char* path)
{
    if (!path || !*path)
        return -1;

    char abs_path[PATH_MAX];
    resolve_user_path(path, abs_path, sizeof(abs_path));

    // Prevent unlinking the root
    if (strcmp(abs_path, "/") == 0)
        return -1;

    return vfs_unlink(abs_path);
}
