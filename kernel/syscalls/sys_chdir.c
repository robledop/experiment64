#include "syscall_common.h"

#include <mem/heap.h>

int sys_chdir(const char* path)
{
    if (!path || !*path)
        return -1;
    char abs_path[PATH_MAX];
    resolve_user_path(path, abs_path, sizeof(abs_path));

    vfs_inode_t* node = vfs_resolve_path(abs_path);
    if (!node)
        return -1;
    if ((node->flags & 0x07) != VFS_DIRECTORY)
    {
        if (node != vfs_root)
        {
            vfs_close(node);
            kfree(node);
        }
        return -1;
    }

    path_safe_copy(current_process->cwd, sizeof(current_process->cwd), abs_path);
    if (node != vfs_root)
    {
        vfs_close(node);
        kfree(node);
    }
    return 0;
}
