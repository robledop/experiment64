#include <unistd.h>
#include <errno.h>
#include <sys/syscall.h>

// VFS_DIRECTORY flag for mknod
#define VFS_DIRECTORY 0x02

int mkdir(const char *path, [[maybe_unused]] int mode)
{
    // Use SYS_MKNOD with VFS_DIRECTORY to create a directory
    (void)mode; // POSIX mode bits not yet supported
    return (int)syscall3(SYS_MKNOD, (long)path, VFS_DIRECTORY, 0);
}

long lseek(int fd, long offset, int whence)
{
    return syscall3(SYS_LSEEK, fd, offset, whence);
}

int remove(const char *path)
{
    return unlink(path);
}

int rename(const char *oldpath, const char *newpath)
{
    if (link(oldpath, newpath) == 0)
    {
        unlink(oldpath);
        return 0;
    }
    return -1;
}

int isatty(int fd)
{
    if (fd >= 0 && fd <= 2)
    {
        return 1;
    }
    errno = -ENOTTY;
    return 0;
}
