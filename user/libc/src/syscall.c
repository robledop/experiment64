#include <sys/syscall.h>
#include <unistd.h>

static inline long syscall0(long n)
{
    unsigned long ret;
    __asm__ __volatile__("syscall" : "=a"(ret) : "a"(n) : "rcx", "r11", "memory");
    return ret;
}

static inline long syscall1(long n, long a1)
{
    unsigned long ret;
    __asm__ __volatile__("syscall" : "=a"(ret) : "a"(n), "D"(a1) : "rcx", "r11", "memory");
    return ret;
}

static inline long syscall2(long n, long a1, long a2)
{
    unsigned long ret;
    __asm__ __volatile__("syscall" : "=a"(ret) : "a"(n), "D"(a1), "S"(a2) : "rcx", "r11", "memory");
    return ret;
}

static inline long syscall3(long n, long a1, long a2, long a3)
{
    unsigned long ret;
    __asm__ __volatile__("syscall" : "=a"(ret) : "a"(n), "D"(a1), "S"(a2), "d"(a3) : "rcx", "r11", "memory");
    return ret;
}

ssize_t write(int fd, const void *buf, size_t count)
{
    return syscall3(SYS_WRITE, fd, (long)buf, count);
}

ssize_t read(int fd, void *buf, size_t count)
{
    return syscall3(SYS_READ, fd, (long)buf, count);
}

int exec(const char *path)
{
    return syscall1(SYS_EXEC, (long)path);
}

void exit(int status)
{
    syscall1(SYS_EXIT, status);
    while (1)
        ;
}

int fork(void)
{
    return syscall0(SYS_FORK);
}

int wait(int *status)
{
    return syscall1(SYS_WAIT, (long)status);
}

int getpid(void)
{
    return syscall0(SYS_GETPID);
}

void yield(void)
{
    syscall0(SYS_YIELD);
}

int spawn(const char *path)
{
    return syscall1(SYS_SPAWN, (long)path);
}
