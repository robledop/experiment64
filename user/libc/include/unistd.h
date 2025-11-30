#pragma once

#include <sys/syscall.h>
#include <sys/stat.h>
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
int fork(void);
int wait(int *status);
int getpid(void);
void yield(void);
int spawn(const char *path);
void *sbrk(intptr_t increment);
int open(const char *path, int flags);
int close(int fd);
int chdir(const char *path);
int link(const char *oldpath, const char *newpath);
int unlink(const char *path);
int stat(const char *path, struct stat *st);
int fstat(int fd, struct stat *st);
int sleep(int milliseconds);
int usleep(unsigned int usec);
int mkdir(const char *path, int mode);
int ioctl(int fd, unsigned long request, void *arg);
void *mmap(void *addr, size_t length, int prot, int flags, int fd, size_t offset);
int munmap(void *addr, size_t length);
char *getcwd(char *buf, size_t size);
long lseek(int fd, long offset, int whence);
int isatty(int fd);
int pipe(int pipefd[2]);
int dup(int oldfd);
void shutdown(void);
void reboot(void);
