#pragma once

#include <stdint.h>
#include <fs/vfs.h>
#include <stddef.h>
#include <net/socket.h>
#include <sys/time.h>
#include <sys/signal.h>

struct syscall_regs
{
    uint64_t rdi, rsi, rdx, r10, r8, r9;
    uint64_t r15, r14, r13, r12, rbx, rbp;
    uint64_t rcx, r11;
};

typedef struct syscall_regs syscall_regs_t;

#define SYS_WRITE 0
#define SYS_READ 1
#define SYS_EXECVE 2
#define SYS_EXIT 3
#define SYS_FORK 4
#define SYS_WAIT 5
#define SYS_GETPID 6
#define SYS_YIELD 7
#define SYS_SPAWN 8
#define SYS_SBRK 9
#define SYS_OPEN 10
#define SYS_CLOSE 11
#define SYS_READDIR 12
#define SYS_CHDIR 13
#define SYS_SLEEP 14
#define SYS_MKNOD 15
#define SYS_IOCTL 16
#define SYS_MMAP 17
#define SYS_MUNMAP 18
#define SYS_STAT 20
#define SYS_FSTAT 21
#define SYS_LINK 22
#define SYS_UNLINK 23
#define SYS_GETCWD 24
#define SYS_GETTIMEOFDAY 25
#define SYS_USLEEP 26
#define SYS_PIPE 27
#define SYS_LSEEK 28
#define SYS_DUP 29
#define SYS_SHUTDOWN 30
#define SYS_REBOOT 31
#define SYS_KILL 32
#define SYS_SOCKET 33
#define SYS_BIND 34
#define SYS_SENDTO 35
#define SYS_RECVFROM 36
#define SYS_LISTEN 37
#define SYS_ACCEPT 38
#define SYS_SIGACTION 39
#define SYS_SIGRETURN 40
#define SYS_THREAD_CREATE 41
#define SYS_THREAD_EXIT 42
#define SYS_THREAD_JOIN 43
#define SYS_GETTID 44
#define SYS_FUTEX_WAIT 45
#define SYS_FUTEX_WAKE 46
#define SYS_THREAD_DETACH 47
#define SYS_WAITPID 48
#define SYS_RENAME 49
#define SYS_ARCH_PRCTL 50

#define ARCH_SET_FS 0x1002
#define ARCH_GET_FS 0x1003

void syscall_init(void);
void syscall_set_exit_hook(void (*hook)(int));
void syscall_set_stack(uint64_t stack_top);

int sys_close(int fd);
int sys_readdir(int fd, vfs_dirent_t* dent);
int64_t sys_sbrk(int64_t increment);
void sys_exit(int code);
int sys_wait(int* status);
int sys_waitpid(int pid, int* status, int options);
int sys_getpid(void);
int sys_read(int fd, char* buf, size_t count);
int sys_write(int fd, const char* buf, size_t count);
int sys_exec(const char* path, struct syscall_regs* regs);
int sys_execve(const char* path, const char* const argv[], const char* const envp[], struct syscall_regs* regs);
int sys_spawn(const char* path);
int sys_fork(struct syscall_regs* regs);
int sys_chdir(const char* path);
int sys_getcwd(char* buf, size_t size);
int sys_sleep(uint64_t milliseconds);
int sys_usleep(uint64_t usec);
int sys_mknod(const char* path, int mode, int dev);
int sys_ioctl(int fd, int request, void* arg);
int sys_open(const char* path, int flags);
void* sys_mmap(void* addr, size_t length, int prot, int flags, int fd, size_t offset);
int sys_munmap(void* addr, size_t length);
int sys_stat(const char* path, struct stat* st);
int sys_fstat(int fd, struct stat* st);
int sys_link(const char* oldpath, const char* newpath);
int sys_unlink(const char* path);
int sys_rename(const char* oldpath, const char* newpath);
int sys_gettimeofday(struct timeval* tv, struct timezone* tz);
int sys_pipe(int pipefd[2]);
long sys_lseek(int fd, long offset, int whence);
int sys_dup(int oldfd);
int sys_kill(int pid, int sig);
int sys_socket(int domain, int type, int protocol);
int sys_bind(int fd, const struct sockaddr* addr, size_t addrlen);
int sys_listen(int fd, int backlog);
int sys_accept(int fd, struct sockaddr* addr, size_t addrlen);
int sys_sendto(int fd, const void* buf, size_t len, int flags,
               const struct sockaddr* dest_addr, socklen_t addrlen);
int sys_recvfrom(int fd, void* buf, size_t len, int flags,
                 struct sockaddr* src_addr, socklen_t* addrlen);
int sys_sigaction(int signum, const sigaction_t* act, sigaction_t* oldact);
uint64_t sys_sigreturn(const sigcontext_t* user_ctx, struct syscall_regs* regs);
int sys_thread_create(uint64_t entry, uint64_t arg);
void sys_thread_exit(int code);
int sys_thread_join(int tid, int* status);
int sys_gettid(void);
int sys_futex_wait(uint32_t* uaddr, uint32_t expected);
int sys_futex_wake(uint32_t* uaddr, int count);
int sys_thread_detach(int tid);
int sys_arch_prctl(int code, uint64_t addr);
void sys_shutdown();
void sys_reboot();

#ifdef TEST_MODE
extern volatile uint64_t test_syscall_count;
extern volatile uint64_t test_syscall_last_num;
extern volatile uint64_t test_syscall_last_arg1;
#endif
