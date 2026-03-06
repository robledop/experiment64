#include <unistd.h>
#include <errno.h>
#include <sys/syscall.h>
#include <sys/ioctl.h>
#include <sys/termios.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <status.h>
#include <string.h>
#include <stdlib.h>

int optind = 1;
char *optarg = nullptr;

static char *getopt_place = nullptr;

int getopt(int argc, char *const argv[], const char *optstring)
{
    if (!argv || !optstring)
        return -1;
    if (optind == 0)
        optind = 1;
    if (!getopt_place || !*getopt_place) {
        if (optind >= argc || !argv[optind] || argv[optind][0] != '-' || argv[optind][1] == '\0')
            return -1;
        if (argv[optind][1] == '-' && argv[optind][2] == '\0') {
            optind++;
            return -1;
        }
        getopt_place = &argv[optind][1];
    }
    char c = *getopt_place++;
    if (!*getopt_place)
        optind++;
    const char *p = strchr(optstring, c);
    if (!p) {
        optarg = nullptr;
        return '?';
    }
    if (p[1] == ':') {
        if (*getopt_place) {
            optarg = getopt_place;
            getopt_place = nullptr;
            optind++;
        } else if (optind < argc) {
            optarg = argv[optind];
            optind++;
            getopt_place = nullptr;
        } else {
            optarg = nullptr;
            return '?';
        }
    } else {
        optarg = nullptr;
    }
    return c;
}

int chown(const char *path, uid_t owner, gid_t group)
{
    (void)path;
    (void)owner;
    (void)group;
    errno = EUNIMP;
    return -1;
}

int lchown(const char *path, uid_t owner, gid_t group)
{
    (void)path;
    (void)owner;
    (void)group;
    errno = EUNIMP;
    return -1;
}

static mode_t current_umask;

mode_t umask(mode_t mask)
{
    mode_t prev = current_umask;
    current_umask = mask & 0777;
    return prev;
}

int chmod(const char *path, mode_t mode)
{
    (void)path;
    (void)mode;
    errno = EUNIMP;
    return -1;
}

int utimes(const char *path, const struct timeval times[2])
{
    (void)path;
    (void)times;
    errno = EUNIMP;
    return -1;
}

int symlink(const char *target, const char *linkpath)
{
    (void)target;
    (void)linkpath;
    errno = EUNIMP;
    return -1;
}

ssize_t readlink(const char *path, char *buf, size_t bufsiz)
{
    (void)path;
    (void)buf;
    (void)bufsiz;
    errno = EUNIMP;
    return -1;
}

unsigned int alarm(unsigned int seconds)
{
    (void)seconds;
    return 0;
}

uid_t getuid(void)
{
    return 0;
}

uid_t geteuid(void)
{
    return 0;
}

gid_t getegid(void)
{
    return 0;
}

int getppid(void)
{
    return 1;
}

gid_t getgid(void)
{
    return 0;
}

int setuid(uid_t uid)
{
    (void)uid;
    errno = EUNIMP;
    return -1;
}

int setgid(gid_t gid)
{
    (void)gid;
    errno = EUNIMP;
    return -1;
}

int seteuid(uid_t uid)
{
    (void)uid;
    errno = EUNIMP;
    return -1;
}

int setegid(gid_t gid)
{
    (void)gid;
    errno = EUNIMP;
    return -1;
}

int fchdir(int fd)
{
    (void)fd;
    errno = EUNIMP;
    return -1;
}

int chroot(const char *path)
{
    (void)path;
    errno = EUNIMP;
    return -1;
}

int ttyname_r(int fd, char *buf, size_t buflen)
{
    (void)fd;
    (void)buf;
    (void)buflen;
    errno = EUNIMP;
    return -1;
}

int access(const char *path, int mode)
{
    struct stat st;
    if (stat(path, &st) != 0)
        return -1;
    (void)mode;
    return 0;
}

void _exit(int status)
{
    _Exit(status);
}

pid_t vfork(void)
{
    return (pid_t)fork();
}

int setsid(void)
{
    errno = EUNIMP;
    return -1;
}

long sysconf(int name)
{
    if (name == _SC_CLK_TCK)
        return 100;
    errno = EUNIMP;
    return -1;
}

int execv(const char *path, char *const argv[])
{
    return execve(path, argv, nullptr);
}

int execvp(const char *file, char *const argv[])
{
    (void)file;
    (void)argv;
    errno = EUNIMP;
    return -1;
}

int getgroups(int size, gid_t list[])
{
    (void)size;
    (void)list;
    return 0;
}

int nanosleep(const struct timespec *req, struct timespec *rem)
{
    if (!req || req->tv_sec < 0 || req->tv_nsec < 0 || req->tv_nsec >= 1000000000)
    {
        errno = EINVAL;
        return -1;
    }
    unsigned long usec = (unsigned long)req->tv_sec * 1000000UL + (unsigned long)(req->tv_nsec / 1000);
    if (usec > 0)
        usleep((unsigned int)usec);
    if (rem)
    {
        rem->tv_sec = 0;
        rem->tv_nsec = 0;
    }
    return 0;
}

// VFS_DIRECTORY flag for mknod
#define VFS_DIRECTORY 0x02

int mkdir(const char *path, [[maybe_unused]] int mode)
{
    // Use SYS_MKNOD with VFS_DIRECTORY to create a directory
    (void)mode; // POSIX mode bits not yet supported
    long ret = syscall3(SYS_MKNOD, (long)path, VFS_DIRECTORY, 0);
    return ret == 0 ? 0 : (int)ret;
}

int rmdir(const char *path)
{
    (void)path;
    errno = EUNIMP;
    return -1;
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
