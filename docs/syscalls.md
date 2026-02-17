# Syscall Return Conventions

General convention:

- Success returns `0` or a positive value.
- Failure returns a negative status code defined in `include/status.h`.

Examples:

- `-EBADF`: invalid file descriptor
- `-EINVAL`: invalid argument
- `-EFAULT`: invalid user pointer
- `-ENOTSUP`: operation or combination not supported
- `-EAGAIN`: temporary condition (for example nonblocking receive with no data)
- `-ENOMEM`: allocation failure

Syscall groups with differentiated status returns:

- User-thread syscalls (`SYS_THREAD_CREATE`, `SYS_THREAD_JOIN`, `SYS_THREAD_DETACH`)
- Socket syscalls (`SYS_SOCKET`, `SYS_BIND`, `SYS_LISTEN`, `SYS_ACCEPT`, `SYS_SENDTO`, `SYS_RECVFROM`)
- Core fd/data-path syscalls (`SYS_OPEN`, `SYS_CLOSE`, `SYS_READ`, `SYS_WRITE`, `SYS_LSEEK`, `SYS_DUP`, `SYS_READDIR`)
