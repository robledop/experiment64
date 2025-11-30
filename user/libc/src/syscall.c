#include <unistd.h>
#include <sys/syscall.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <stdlib.h>
#include <util.h>
#include <stdbool.h>
#include <termios.h>

#undef exit

ssize_t write(int fd, const void *buf, size_t count)
{
    // Honor basic OPOST: map '\n' -> "\r\n" for terminal FDs when enabled.
    tcflag_t oflags = __termios_get_oflag(fd);
    const bool do_post = (oflags & OPOST) && isatty(fd);

    if (!do_post || count == 0)
        return syscall3(SYS_WRITE, fd, (long)buf, (long)count);

    const char *in = (const char *)buf;
    ssize_t total_src_written = 0;

    char tmp[256];
    while (total_src_written < (ssize_t)count)
    {
        size_t out_len = 0;
        size_t slice_src = 0;
        while ((total_src_written + (ssize_t)slice_src) < (ssize_t)count &&
               out_len + 2 <= sizeof(tmp))
        {
            char c = in[total_src_written + (ssize_t)slice_src];
            if (c == '\n')
            {
                tmp[out_len++] = '\r';
                tmp[out_len++] = '\n';
            }
            else
            {
                tmp[out_len++] = c;
            }
            slice_src++;
        }

        ssize_t res = syscall3(SYS_WRITE, fd, (long)tmp, (long)out_len);
        if (res < 0)
            return (total_src_written > 0) ? total_src_written : res;

        if ((size_t)res == out_len)
        {
            total_src_written += (ssize_t)slice_src;
            continue;
        }

        // Partial write; map back from produced bytes to source bytes.
        size_t produced = 0;
        size_t consumed_src = 0;
        while (consumed_src < slice_src && produced < (size_t)res)
        {
            if (in[total_src_written + (ssize_t)consumed_src] == '\n')
            {
                if (produced + 2 > (size_t)res)
                    break;
                produced += 2;
            }
            else
            {
                produced += 1;
            }
            consumed_src++;
        }
        total_src_written += (ssize_t)consumed_src;
        return total_src_written;
    }

    return total_src_written;
}

ssize_t read(int fd, void *buf, size_t count)
{
    return syscall3(SYS_READ, fd, (long)buf, (long)count);
}

int exec(const char *path)
{
    char *const argv[] = {(char *)path, nullptr};
    return clamp_signed_to_int(syscall3(SYS_EXECVE, (long)path, (long)argv, 0));
}

int execve(const char *path, char *const argv[], char *const envp[])
{
    (void)envp; // envp is currently ignored by the kernel
    return clamp_signed_to_int(syscall3(SYS_EXECVE, (long)path, (long)argv, (long)envp));
}

[[noreturn]] void __exit_impl(int status)
{
    syscall1(SYS_EXIT, status);
    while (1)
        ;
}

void __exit_with_handlers(int status)
{
    __libc_run_atexit();
    __exit_impl(status);

    __builtin_unreachable();
}

void exit(int status)
{
    __exit_with_handlers(status);
    __builtin_unreachable();
}

int fork(void)
{
    return clamp_signed_to_int(syscall0(SYS_FORK));
}

int wait(int *status)
{
    return clamp_signed_to_int(syscall1(SYS_WAIT, (long)status));
}

int getpid(void)
{
    return clamp_signed_to_int(syscall0(SYS_GETPID));
}

void yield(void)
{
    syscall0(SYS_YIELD);
}

int spawn(const char *path)
{
    return clamp_signed_to_int(syscall1(SYS_SPAWN, (long)path));
}

void *sbrk(intptr_t increment)
{
    return (void *)syscall1(SYS_SBRK, (long)increment);
}

int open(const char *path, int flags)
{
    return clamp_signed_to_int(syscall2(SYS_OPEN, (long)path, flags));
}

int close(int fd)
{
    return clamp_signed_to_int(syscall1(SYS_CLOSE, fd));
}

int sys_readdir(int fd, void *dent)
{
    return clamp_signed_to_int(syscall2(SYS_READDIR, fd, (long)dent));
}

int chdir(const char *path)
{
    return clamp_signed_to_int(syscall1(SYS_CHDIR, (long)path));
}

int link(const char *oldpath, const char *newpath)
{
    return clamp_signed_to_int(syscall2(SYS_LINK, (long)oldpath, (long)newpath));
}

int unlink(const char *path)
{
    return clamp_signed_to_int(syscall1(SYS_UNLINK, (long)path));
}

int stat(const char *path, struct stat *st)
{
    return clamp_signed_to_int(syscall2(SYS_STAT, (long)path, (long)st));
}

int fstat(int fd, struct stat *st)
{
    return clamp_signed_to_int(syscall2(SYS_FSTAT, fd, (long)st));
}

int sleep(int milliseconds)
{
    if (milliseconds < 0)
        milliseconds = 0;
    return clamp_signed_to_int(syscall1(SYS_SLEEP, milliseconds));
}

int usleep(unsigned int usec)
{
    return clamp_signed_to_int(syscall1(SYS_USLEEP, (long)usec));
}

int ioctl(int fd, unsigned long request, void *arg)
{
    return clamp_signed_to_int(syscall3(SYS_IOCTL, fd, (long)request, (long)arg));
}

char *getcwd(char *buf, size_t size)
{
    long ret = syscall2(SYS_GETCWD, (long)buf, (long)size);
    if (ret < 0)
        return nullptr;
    return buf;
}

int gettimeofday(struct timeval *tv, struct timezone *tz)
{
    return clamp_signed_to_int(syscall2(SYS_GETTIMEOFDAY, (long)tv, (long)tz));
}

void *mmap(void *addr, size_t length, int prot, int flags, int fd, size_t offset)
{
    long ret = syscall6(SYS_MMAP, (long)addr, (long)length, prot, flags, fd, (long)offset);
    if (ret < 0)
        return MAP_FAILED;
    return (void *)ret;
}

int munmap(void *addr, size_t length)
{
    return clamp_signed_to_int(syscall2(SYS_MUNMAP, (long)addr, (long)length));
}

int pipe(int pipefd[2])
{
    return clamp_signed_to_int(syscall1(SYS_PIPE, (long)pipefd));
}

int dup(int oldfd)
{
    return clamp_signed_to_int(syscall1(SYS_DUP, oldfd));
}

void shutdown(void)
{
    syscall0(SYS_SHUTDOWN);
}

void reboot(void)
{
    syscall0(SYS_REBOOT);
}
