#include <unistd.h>
#include <sys/syscall.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <util.h>

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

static inline long syscall6(long n, long a1, long a2, long a3, long a4, long a5, long a6)
{
    long ret;
    register long r10 __asm__("r10") = a4;
    register long r8 __asm__("r8") = a5;
    register long r9 __asm__("r9") = a6;
    __asm__ __volatile__("syscall"
                         : "=a"(ret)
                         : "a"(n), "D"(a1), "S"(a2), "d"(a3), "r"(r10), "r"(r8), "r"(r9)
                         : "rcx", "r11", "memory");
    return ret;
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
    char *const argv[] = {(char *)path, nullptr};
    return clamp_signed_to_int(syscall3(SYS_EXECVE, (long)path, (long)argv, 0));
}

int execve(const char *path, char *const argv[], char *const envp[])
{
    (void)envp; // envp is currently ignored by the kernel
    return clamp_signed_to_int(syscall3(SYS_EXECVE, (long)path, (long)argv, (long)envp));
}

void __exit_impl(int status)
{
    syscall1(SYS_EXIT, status);
    while (1)
        ;
}

void exit(int status)
{
    __exit_impl(status);

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

int ioctl(int fd, unsigned long request, void *arg)
{
    return clamp_signed_to_int(syscall3(SYS_IOCTL, fd, (long)request, (long)arg));
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
