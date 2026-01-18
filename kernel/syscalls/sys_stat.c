#include "syscall_common.h"

#include <mem/heap.h>

int sys_stat(const char* path, struct stat* st)
{
    if (!path || !st)
        return -1;
    if (!user_ptr_write_ok(st, sizeof(*st), "sys_stat"))
        return -1;

    char abs_path[PATH_MAX];
    resolve_user_path(path, abs_path, sizeof(abs_path));

    vfs_inode_t* inode = vfs_resolve_path(abs_path);
    if (!inode)
        return -1;

    fill_stat_from_inode(inode, st);
    if (inode != vfs_root)
    {
        vfs_close(inode);
        kfree(inode);
    }
    return 0;
}
