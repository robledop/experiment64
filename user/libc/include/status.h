#pragma once

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

static inline char *strerror(const int error)
{
    switch (error) {
    case -EIO:
        return "Input/output error";
    case -EINVARG:
        return "Invalid argument";
    case -ENOMEM:
        return "Out of memory";
    case -EBADPATH:
        return "Invalid path";
    case -EFSNOTUS:
        return "File system not supported";
    case -ERDONLY:
        return "Read only file system";
    case -EUNIMP:
        return "Operation not implemented";
    case -EINSTKN:
        return "Instance already taken";
    case -EINFORMAT:
        return "Invalid format";
    case -ENOENT:
        return "No such file or directory";
    case -EAGAIN:
        return "Resource temporarily unavailable";
    case -ENOTDIR:
        return "Not a directory";
    case -EBADF:
        return "Bad file descriptor";
    case -EFAULT:
        return "Invalid memory address";
    case -ENOTSUP:
        return "Operation not supported";
    case -EISDIR:
        return "Is a directory";
    default:
        return "Unknown error";
    }
}
