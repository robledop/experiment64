#include <unistd.h>
#include <errno.h>
#include <sys/syscall.h>
#include <sys/ioctl.h>
#include <sys/termios.h>

// VFS_DIRECTORY flag for mknod
#define VFS_DIRECTORY 0x02

int mkdir(const char *path, [[maybe_unused]] int mode)
{
    // Use SYS_MKNOD with VFS_DIRECTORY to create a directory
    (void)mode; // POSIX mode bits not yet supported
    long ret = syscall3(SYS_MKNOD, (long)path, VFS_DIRECTORY, 0);
    if (ret < 0) {
        errno = (int)-ret;
        return -1;
    }
    return (int)ret;
}

long lseek(int fd, long offset, int whence)
{
    long ret = syscall3(SYS_LSEEK, fd, offset, whence);
    if (ret < 0) {
        errno = (int)-ret;
        return -1;
    }
    return ret;
}

int remove(const char *path)
{
    return unlink(path);
}

int rename(const char *oldpath, const char *newpath)
{
    long ret = syscall2(SYS_RENAME, (long)oldpath, (long)newpath);
    if (ret < 0) {
        errno = (int)-ret;
        return -1;
    }
    return (int)ret;
}

int isatty(int fd)
{
    struct termios termios_state = {};
    if (ioctl(fd, TIOCGETA, &termios_state) == 0)
        return 1;

    if (errno == 0)
        errno = ENOTTY;
    return 0;
}
