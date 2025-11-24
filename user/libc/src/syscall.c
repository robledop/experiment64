#include <unistd.h>
#include <sys/syscall.h>
#include <limits.h>

static inline long syscall0(long n)
{
    long ret;
    __asm__ __volatile__("syscall" : "=a"(ret) : "a"(n) : "rcx", "r11", "memory");
    return ret;
}

static inline long syscall1(long n, long a1)
{
    long ret;
    __asm__ __volatile__("syscall" : "=a"(ret) : "a"(n), "D"(a1) : "rcx", "r11", "memory");
    return ret;
}

static inline long syscall2(long n, long a1, long a2)
{
    long ret;
    __asm__ __volatile__("syscall" : "=a"(ret) : "a"(n), "D"(a1), "S"(a2) : "rcx", "r11", "memory");
    return ret;
}

static inline long syscall3(long n, long a1, long a2, long a3)
{
    long ret;
    __asm__ __volatile__("syscall" : "=a"(ret) : "a"(n), "D"(a1), "S"(a2), "d"(a3) : "rcx", "r11", "memory");
    return ret;
}

static int clamp_long_to_int(long value)
{
    if (value > INT_MAX)
        return INT_MAX;
    if (value < INT_MIN)
        return INT_MIN;
    return (int)value;
}

ssize_t write(int fd, const void *buf, size_t count)
{
    return syscall3(SYS_WRITE, fd, (long)buf, (long)count);
}

ssize_t read(int fd, void *buf, size_t count)
{
    return syscall3(SYS_READ, fd, (long)buf, (long)count);
}

int exec(const char *path)
{
    return clamp_long_to_int(syscall1(SYS_EXEC, (long)path));
}

void exit(int status)
{
    syscall1(SYS_EXIT, status);
    while (1)
        ;
}

int fork(void)
{
    return clamp_long_to_int(syscall0(SYS_FORK));
}

int wait(int *status)
{
    return clamp_long_to_int(syscall1(SYS_WAIT, (long)status));
}

int getpid(void)
{
    return clamp_long_to_int(syscall0(SYS_GETPID));
}

void yield(void)
{
    syscall0(SYS_YIELD);
}

int spawn(const char *path)
{
    return clamp_long_to_int(syscall1(SYS_SPAWN, (long)path));
}

void *sbrk(intptr_t increment)
{
    return (void *)syscall1(SYS_SBRK, (long)increment);
}

int open(const char *path)
{
    return clamp_long_to_int(syscall1(SYS_OPEN, (long)path));
}

int close(int fd)
{
    return clamp_long_to_int(syscall1(SYS_CLOSE, fd));
}

int sys_readdir(int fd, void *dent)
{
    return clamp_long_to_int(syscall2(SYS_READDIR, fd, (long)dent));
}

int chdir(const char *path)
{
    return clamp_long_to_int(syscall1(SYS_CHDIR, (long)path));
}

int sleep(int milliseconds)
{
    if (milliseconds < 0)
        milliseconds = 0;
    return clamp_long_to_int(syscall1(SYS_SLEEP, milliseconds));
}
