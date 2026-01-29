#include <syscall_common.h>
#include <fs/vfs.h>
#include <lib/path.h>

int sys_mknod(const char* path, int mode, int dev)
{
    char kpath[PATH_MAX];
    if (!copy_from_user_str(kpath, path, sizeof(kpath)))
        return -1;
    if (kpath[0] == '\0')
        return -1;
    path_simplify(kpath, sizeof(kpath));

    return vfs_mknod(kpath, mode, dev);
}
