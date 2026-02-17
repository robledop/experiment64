#pragma once

// Keep values in sync with user/libc/include/status.h.

#define ALL_OK 0
// Input/output error
#define EIO 1
// Invalid argument
#define EINVARG 2
// Out of memory
#define ENOMEM 3
// Invalid path
#define EBADPATH 4
// File system not supported
#define EFSNOTUS 5
// Read only file system
#define ERDONLY 6
// Operation not implemented
#define EUNIMP 7
// Instance already taken
#define EINSTKN 8
// Invalid format
#define EINFORMAT 9
// No such file or directory
#define ENOENT 10
// Resource temporarily unavailable
#define EAGAIN 11
// Not a directory
#define ENOTDIR 12
// Bad file descriptor
#define EBADF 13
// Invalid memory address
#define EFAULT 14
// Operation not supported
#define ENOTSUP 15
// Buffer full
#define EBUFFULL 16
// Not a typewriter
#define ENOTTY 17
// Is a directory
#define EISDIR 18
// Operation not permitted
#define EPERM 19
// Device or resource busy
#define EBUSY 20
// Resource deadlock would occur
#define EDEADLK 21
// Operation timed out
#define ETIMEDOUT 22
// Interrupted system call
#define EINTR 23

// POSIX aliases mapped to existing project status codes.
#define EINVAL EINVARG
#define ESRCH ENOENT
#define ENOSYS EUNIMP
