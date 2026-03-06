#pragma once

#include <sys/syscall.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <stddef.h>
#include <stdint.h>

typedef long ssize_t;

#define STDIN_FILENO 0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2

ssize_t write(int fd, const void *buf, size_t count);
ssize_t read(int fd, void *buf, size_t count);
int exec(const char *path);
int execve(const char *path, char *const argv[], char *const envp[]);
int execv(const char *path, char *const argv[]);
int execvp(const char *file, char *const argv[]);
int fork(void);
pid_t vfork(void);
int wait(int *status);
int setsid(void);
int waitpid(int pid, int *status, int options);
int getpid(void);
int gettid(void);
void yield(void);
int spawn(const char *path);
int thread_create(void (*entry)(void *), void *arg);
[[noreturn]] void thread_exit(int status);
int thread_join(int tid, int *status);
int thread_detach(int tid);
int futex_wait(volatile int *addr, int expected);
int futex_wake(volatile int *addr, int count);
void *sbrk(intptr_t increment);
int open(const char *path, int flags, ...);
int close(int fd);
int chdir(const char *path);
int link(const char *oldpath, const char *newpath);
int symlink(const char *target, const char *linkpath);
ssize_t readlink(const char *path, char *buf, size_t bufsiz);
int unlink(const char *path);
int stat(const char *path, struct stat *st);
int lstat(const char *path, struct stat *st);
int fstat(int fd, struct stat *st);
int mknod(const char *path, mode_t mode, dev_t dev);
int chown(const char *path, uid_t owner, gid_t group);
int lchown(const char *path, uid_t owner, gid_t group);
mode_t umask(mode_t mask);
int chmod(const char *path, mode_t mode);
int utimes(const char *path, const struct timeval times[2]);
int sleep(int milliseconds);
int usleep(unsigned int usec);
unsigned int alarm(unsigned int seconds);
int mkdir(const char *path, int mode);
int rmdir(const char *path);
int ioctl(int fd, unsigned long request, void *arg);
void *mmap(void *addr, size_t length, int prot, int flags, int fd, size_t offset);
int munmap(void *addr, size_t length);
char *getcwd(char *buf, size_t size);
long lseek(int fd, long offset, int whence);
int ftruncate(int fd, long length);
int isatty(int fd);
int pipe(int pipefd[2]);
int dup(int oldfd);
int dup2(int oldfd, int newfd);
int kill(int pid, int sig);
void shutdown(void);
void reboot(void);
[[noreturn]] void _exit(int status);

#define _SC_CLK_TCK 1
long sysconf(int name);

int getgroups(int size, gid_t list[]);

uid_t getuid(void);
uid_t geteuid(void);
gid_t getegid(void);
int getppid(void);
gid_t getgid(void);

int setuid(uid_t uid);
int setgid(gid_t gid);
int seteuid(uid_t uid);
int setegid(gid_t gid);
int fchdir(int fd);
int chroot(const char *path);
int ttyname_r(int fd, char *buf, size_t buflen);

#define F_OK 0
#define R_OK 4
#define W_OK 2
#define X_OK 1
int access(const char *path, int mode);

int getopt(int argc, char *const argv[], const char *optstring);

extern int optind;
extern char *optarg;
extern char **environ;
