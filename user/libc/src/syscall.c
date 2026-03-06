#include <unistd.h>
#include <sys/syscall.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <poll.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdarg.h>
#include <util.h>
#include <termios.h>
#include <errno.h>

#undef exit

static int syscall_to_int(const long ret)
{
    if (ret < 0) {
        errno = (int)-ret;
        return -1;
    }
    return clamp_signed_to_int(ret);
}

static ssize_t syscall_to_ssize(const long ret)
{
    if (ret < 0) {
        errno = (int)-ret;
        return -1;
    }
    return (ssize_t)ret;
}

#define DEFINE_ERRNO_INT0(name, sysno) \
    int name(void) \
    { \
        return syscall_to_int(syscall0(sysno)); \
    }

#define DEFINE_ERRNO_INT1(name, sysno, t1, a1) \
    int name(t1 a1) \
    { \
        return syscall_to_int(syscall1(sysno, (long)(a1))); \
    }

#define DEFINE_ERRNO_INT2(name, sysno, t1, a1, t2, a2) \
    int name(t1 a1, t2 a2) \
    { \
        return syscall_to_int(syscall2(sysno, (long)(a1), (long)(a2))); \
    }

#define DEFINE_ERRNO_INT3(name, sysno, t1, a1, t2, a2, t3, a3) \
    int name(t1 a1, t2 a2, t3 a3) \
    { \
        return syscall_to_int(syscall3(sysno, (long)(a1), (long)(a2), (long)(a3))); \
    }

#define DEFINE_CLAMP_INT1(name, sysno, t1, a1) \
    int name(t1 a1) \
    { \
        return clamp_signed_to_int(syscall1(sysno, (long)(a1))); \
    }

#define DEFINE_CLAMP_INT2(name, sysno, t1, a1, t2, a2) \
    int name(t1 a1, t2 a2) \
    { \
        return clamp_signed_to_int(syscall2(sysno, (long)(a1), (long)(a2))); \
    }

#define DEFINE_ERRNO_SSIZE3(name, sysno, t1, a1, t2, a2, t3, a3) \
    ssize_t name(t1 a1, t2 a2, t3 a3) \
    { \
        return syscall_to_ssize(syscall3(sysno, (long)(a1), (long)(a2), (long)(a3))); \
    }

#define DEFINE_VOID0(name, sysno) \
    void name(void) \
    { \
        syscall0(sysno); \
    }

ssize_t write(int fd, const void *buf, size_t count)
{
    // Honor basic OPOST: map '\n' -> "\r\n" for terminal FDs when enabled.
    tcflag_t oflags    = __termios_get_oflag(fd);
    const bool do_post = (oflags & OPOST) && isatty(fd);

    if (!do_post || count == 0)
        return syscall_to_ssize(syscall3(SYS_WRITE, fd, (long)buf, (long)count));

    const char *in            = (const char *)buf;
    ssize_t total_src_written = 0;

    char tmp[256];
    while (total_src_written < (ssize_t)count) {
        size_t out_len   = 0;
        size_t slice_src = 0;
        while ((total_src_written + (ssize_t)slice_src) < (ssize_t)count &&
            out_len + 2 <= sizeof(tmp)) {
            char c = in[total_src_written + (ssize_t)slice_src];
            if (c == '\n') {
                tmp[out_len++] = '\r';
                tmp[out_len++] = '\n';
            } else {
                tmp[out_len++] = c;
            }
            slice_src++;
        }

        ssize_t res = syscall3(SYS_WRITE, fd, (long)tmp, (long)out_len);
        if (res < 0) {
            if (total_src_written > 0)
                return total_src_written;
            errno = (int)-res;
            return -1;
        }

        if ((size_t)res == out_len) {
            total_src_written += (ssize_t)slice_src;
            continue;
        }

        // Partial write; map back from produced bytes to source bytes.
        size_t produced     = 0;
        size_t consumed_src = 0;
        while (consumed_src < slice_src && produced < (size_t)res) {
            if (in[total_src_written + (ssize_t)consumed_src] == '\n') {
                if (produced + 2 > (size_t)res)
                    break;
                produced += 2;
            } else {
                produced += 1;
            }
            consumed_src++;
        }
        total_src_written += (ssize_t)consumed_src;
        return total_src_written;
    }

    return total_src_written;
}

DEFINE_ERRNO_SSIZE3(read, SYS_READ, int, fd, void *, buf, size_t, count)

int exec(const char *path)
{
    const char *argv[] = {(char *)path, nullptr};
    return syscall_to_int(syscall3(SYS_EXECVE, (long)path, (long)argv, 0));
}

int execve(const char *path, char *const argv[], char *const envp[])
{
    (void)envp; // envp is currently ignored by the kernel
    return syscall_to_int(syscall3(SYS_EXECVE, (long)path, (long)argv, (long)envp));
}

[[noreturn]] void __exit_impl(int status)
{
    syscall1(SYS_EXIT, status);
    while (1);
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

DEFINE_ERRNO_INT0(fork, SYS_FORK)
DEFINE_ERRNO_INT1(wait, SYS_WAIT, int *, status)
DEFINE_ERRNO_INT3(waitpid, SYS_WAITPID, int, pid, int *, status, int, options)
DEFINE_ERRNO_INT0(getpid, SYS_GETPID)
DEFINE_ERRNO_INT0(gettid, SYS_GETTID)
DEFINE_VOID0(yield, SYS_YIELD)
DEFINE_ERRNO_INT1(spawn, SYS_SPAWN, const char *, path)
int thread_create(void (*entry)(void *), void *arg)
{
    return clamp_signed_to_int(syscall2(SYS_THREAD_CREATE, (long)entry, (long)arg));
}

[[noreturn]] void thread_exit(int status)
{
    syscall1(SYS_THREAD_EXIT, status);
    panic("thread_exit: syscall returned unexpectedly");
    __builtin_unreachable();
}

DEFINE_CLAMP_INT2(thread_join, SYS_THREAD_JOIN, int, tid, int *, status)
DEFINE_CLAMP_INT1(thread_detach, SYS_THREAD_DETACH, int, tid)
DEFINE_CLAMP_INT2(futex_wait, SYS_FUTEX_WAIT, volatile int *, addr, int, expected)
DEFINE_CLAMP_INT2(futex_wake, SYS_FUTEX_WAKE, volatile int *, addr, int, count)

void *sbrk(intptr_t increment)
{
    long ret = syscall1(SYS_SBRK, (long)increment);
    if (ret < 0) {
        errno = (int)-ret;
        return (void *)-1;
    }
    return (void *)ret;
}

int open(const char *path, int flags, ...)
{
    return syscall_to_int(syscall2(SYS_OPEN, (long)path, flags));
}

DEFINE_ERRNO_INT1(close, SYS_CLOSE, int, fd)
DEFINE_CLAMP_INT2(sys_readdir, SYS_READDIR, int, fd, void *, dent)
DEFINE_ERRNO_INT1(chdir, SYS_CHDIR, const char *, path)
DEFINE_ERRNO_INT2(link, SYS_LINK, const char *, oldpath, const char *, newpath)
DEFINE_ERRNO_INT1(unlink, SYS_UNLINK, const char *, path)
DEFINE_ERRNO_INT2(stat, SYS_STAT, const char *, path, struct stat *, st)

int lstat(const char *path, struct stat *st)
{
    return stat(path, st);
}

DEFINE_ERRNO_INT3(mknod, SYS_MKNOD, const char *, path, mode_t, mode, dev_t, dev)
DEFINE_ERRNO_INT2(fstat, SYS_FSTAT, int, fd, struct stat *, st)

int sleep(int milliseconds)
{
    if (milliseconds < 0)
        milliseconds = 0;
    return syscall_to_int(syscall1(SYS_SLEEP, milliseconds));
}

DEFINE_ERRNO_INT1(usleep, SYS_USLEEP, unsigned int, usec)
DEFINE_ERRNO_INT3(ioctl, SYS_IOCTL, int, fd, unsigned long, request, void *, arg)

char *getcwd(char *buf, size_t size)
{
    long ret = syscall2(SYS_GETCWD, (long)buf, (long)size);
    if (ret < 0) {
        errno = (int)-ret;
        return nullptr;
    }
    return buf;
}

DEFINE_ERRNO_INT2(gettimeofday, SYS_GETTIMEOFDAY, struct timeval *, tv, struct timezone *, tz)

void *mmap(void *addr, size_t length, int prot, int flags, int fd, size_t offset)
{
    long ret = syscall6(SYS_MMAP, (long)addr, (long)length, prot, flags, fd, (long)offset);
    if (ret < 0) {
        errno = (int)-ret;
        return MAP_FAILED;
    }
    return (void *)ret;
}

DEFINE_ERRNO_INT2(munmap, SYS_MUNMAP, void *, addr, size_t, length)
DEFINE_ERRNO_INT1(pipe, SYS_PIPE, int *, pipefd)
DEFINE_ERRNO_INT1(dup, SYS_DUP, int, oldfd)
DEFINE_ERRNO_INT2(dup2, SYS_DUP2, int, oldfd, int, newfd)

DEFINE_ERRNO_INT2(ftruncate, SYS_FTRUNCATE, int, fd, long, length)

int fcntl(int fd, int cmd, ...)
{
    long arg = 0;
    if (cmd == F_SETFD || cmd == F_SETFL) {
        va_list ap;
        va_start(ap, cmd);
        arg = (long)va_arg(ap, int);
        va_end(ap);
    }
    return syscall_to_int(syscall3(SYS_FCNTL, fd, cmd, arg));
}

DEFINE_ERRNO_INT3(poll, SYS_POLL, struct pollfd *, fds, nfds_t, nfds, int, timeout)
DEFINE_ERRNO_INT1(openpty, SYS_OPENPTY, int *, fds)
DEFINE_ERRNO_INT2(kill, SYS_KILL, int, pid, int, sig)
DEFINE_VOID0(shutdown, SYS_SHUTDOWN)
DEFINE_VOID0(reboot, SYS_REBOOT)
DEFINE_ERRNO_INT3(socket, SYS_SOCKET, int, domain, int, type, int, protocol)

/**
 * @brief Bind a socket to an address
 * @param sockfd The socket file descriptor
 * @param addr The address to bind to
 * @param addrlen The length of the address
 * @return
 */
DEFINE_ERRNO_INT3(bind, SYS_BIND, int, sockfd, const struct sockaddr *, addr, size_t, addrlen)

/**
 * @brief Mark a socket as listening for incoming connections
 * @param sockfd The socket file descriptor
 * @param backlog The maximum pending connection queue length
 * @return 0 on success, or -1 on error
 */
DEFINE_ERRNO_INT2(listen, SYS_LISTEN, int, sockfd, int, backlog)

/**
 * @brief Accept an incoming connection on a listening socket
 * @param sockfd The listening socket file descriptor
 * @param addr The buffer to receive the peer address
 * @param addrlen The size of the address buffer
 * @return A new socket descriptor, or -1 on error
 */
DEFINE_ERRNO_INT3(accept, SYS_ACCEPT, int, sockfd, struct sockaddr *, addr, socklen_t, addrlen)

/**
 * @brief Send data on a connected socket
 * @param sockfd The socket file descriptor
 * @param buf The buffer containing the data to send
 * @param len The length of the data to send
 * @param flags Flags for the send operation
 * @return The number of bytes sent, or -1 on error
 */
ssize_t send(int sockfd, const void *buf, size_t len, int flags)
{
    return sendto(sockfd, buf, len, flags, nullptr, 0);
}

/**
 * @brief Send data to a specific address using a socket
 * @param sockfd The socket file descriptor
 * @param buf The buffer containing the data to send
 * @param len The length of the data to send
 * @param flags Flags for the send operation
 * @param dest_addr The destination address
 * @param addrlen The length of the destination address
 * @return The number of bytes sent, or -1 on error
 */
ssize_t sendto(int sockfd, const void *buf, size_t len, int flags,
               const struct sockaddr *dest_addr, socklen_t addrlen)
{
    return syscall_to_ssize(syscall6(SYS_SENDTO, sockfd, (long)buf, (long)len, flags, (long)dest_addr, (long)addrlen));
}

/**
 * @brief Receive data from a socket
 * @param sockfd The socket file descriptor
 * @param buf The buffer to store the received data
 * @param len The maximum length of the buffer
 * @param flags Flags for the reception operation
 * @param src_addr The source address
 * @param addrlen The length of the source address
 * @return The number of bytes received, or -1 on error
 */
ssize_t recvfrom(int sockfd, void *buf, size_t len, int flags,
                 struct sockaddr *src_addr, socklen_t *addrlen)
{
    return syscall_to_ssize(syscall6(SYS_RECVFROM, sockfd, (long)buf, (long)len, flags, (long)src_addr, (long)addrlen));
}

DEFINE_ERRNO_INT3(shm_open, SYS_SHM_OPEN, const char *, name, int, flags, size_t, size)
DEFINE_ERRNO_INT1(shm_unlink, SYS_SHM_UNLINK, const char *, name)

#undef DEFINE_ERRNO_INT0
#undef DEFINE_ERRNO_INT1
#undef DEFINE_ERRNO_INT2
#undef DEFINE_ERRNO_INT3
#undef DEFINE_CLAMP_INT1
#undef DEFINE_CLAMP_INT2
#undef DEFINE_ERRNO_SSIZE3
#undef DEFINE_VOID0
