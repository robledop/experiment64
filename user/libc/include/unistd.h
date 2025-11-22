#ifndef _UNISTD_H
#define _UNISTD_H

#include <sys/syscall.h>
#include <stddef.h>

typedef long ssize_t;

ssize_t write(int fd, const void *buf, size_t count);
ssize_t read(int fd, void *buf, size_t count);
int exec(const char *path);
void exit(int status);
int fork(void);
int wait(int *status);
int getpid(void);
void yield(void);
int spawn(const char *path);

#endif
