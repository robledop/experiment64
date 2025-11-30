#include <unistd.h>
#include <errno.h>

int mkdir(const char *path, [[maybe_unused]] int mode)
{
    // No directory creation support yet; pretend success for simple ports.
    (void)path;
    (void)mode;
    return 0;
}

long lseek(int fd, long offset, int whence)
{
    // Not supported; callers using buffered FILE should not rely on it.
    (void)fd;
    (void)offset;
    (void)whence;
    return -1;
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
    if (fd >= 0 && fd <= 2) {
        return 1;
    }
    errno = -ENOTTY;
    return 0;
}
