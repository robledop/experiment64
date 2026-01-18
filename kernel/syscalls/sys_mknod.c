#include <fs/vfs.h>
#include <lib/path.h>

int sys_mknod(const char* path, int mode, int dev)
{
    if ((uint64_t)path >= 0x800000000000) // Check if user pointer
        return -1;

    char kpath[PATH_MAX];
    path_safe_copy(kpath, sizeof(kpath), path);
    path_simplify(kpath, sizeof(kpath));

    return vfs_mknod(kpath, mode, dev);
}
