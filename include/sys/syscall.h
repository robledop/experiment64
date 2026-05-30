#pragma once

#include <uapi/sys/syscall_defs.h>
#include <stdint.h>
#include <fs/vfs.h>
#include <stddef.h>
#include <net/socket.h>
#include <sys/time.h>
#include <sys/signal.h>
#include <sys/wait.h>
#include <sys/poll.h>

// Field order mirrors (reversed) the register push sequence in
// kernel/syscalls/syscall_entry.S: it pushes r11 first and rdi last, so the
// post-push RSP is a valid `struct syscall_regs *`. Keep the two in sync.
struct syscall_regs
{
    uint64_t rdi, rsi, rdx, r10, r8, r9;
    uint64_t r15, r14, r13, r12, rbx, rbp;
    uint64_t rcx, r11;
};

typedef struct syscall_regs syscall_regs_t;

void syscall_init(void);
void syscall_set_exit_hook(void (*hook)(int));
void syscall_set_stack(uint64_t stack_top);

int sys_close(int fd);
int sys_readdir(int fd, vfs_dirent_t* dent);
int64_t sys_sbrk(int64_t increment);
void sys_exit(int code);
int sys_wait(int* status);
int sys_waitpid(int pid, int* status, int options);
int sys_wait4(int pid, int *status, int options, crash_info_t *info);
int sys_getpid(void);
int sys_read(int fd, char* buf, size_t count);
int sys_write(int fd, const char* buf, size_t count);
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
int sys_ftruncate(int fd, long length);
int sys_poll(struct pollfd *fds, long nfds, int timeout);
int sys_fcntl(int fd, int cmd, long arg);
int sys_sigprocmask(int how, const sigset_t *set, sigset_t *oldset);
int sys_dup(int oldfd);
int sys_kill(int pid, int sig);
int sys_socket(int domain, int type, int protocol);
int sys_bind(int fd, const struct sockaddr* addr, size_t addrlen);
int sys_listen(int fd, int backlog);
int sys_accept(int fd, struct sockaddr* addr, socklen_t* addrlen);
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
int sys_shm_open(const char *name, int flags, size_t size);
int sys_shm_unlink(const char *name);
int sys_dup2(int oldfd, int newfd);
int sys_openpty(int fds[2]);
void sys_shutdown();
void sys_reboot();

#ifdef TEST_MODE
extern volatile uint64_t test_syscall_count;
extern volatile uint64_t test_syscall_last_num;
extern volatile uint64_t test_syscall_last_arg1;
#endif
