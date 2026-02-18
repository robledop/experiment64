# Syscall Return Conventions

General convention:

- Success returns `0` or a positive value.
- Failure returns a negative status code defined in `include/status.h`.

Examples:

- `-EBADF`: invalid file descriptor
- `-EINVAL`: invalid argument
- `-EFAULT`: invalid user pointer
- `-EBADPATH`: invalid or empty path
- `-ENOENT`: missing file/path component
- `-ENOTDIR`: non-directory used where directory is required
- `-EISDIR`: directory used where regular file is required
- `-EPERM`: operation not permitted on the target
- `-ENOTSUP`: operation or combination not supported
- `-EAGAIN`: temporary condition (for example nonblocking receive with no data)
- `-ENOMEM`: allocation failure

Syscall groups with differentiated status returns:

- User-thread syscalls (`SYS_THREAD_CREATE`, `SYS_THREAD_JOIN`, `SYS_THREAD_DETACH`)
- Socket syscalls (`SYS_SOCKET`, `SYS_BIND`, `SYS_LISTEN`, `SYS_ACCEPT`, `SYS_SENDTO`, `SYS_RECVFROM`)
- Core fd/data-path syscalls (`SYS_OPEN`, `SYS_CLOSE`, `SYS_READ`, `SYS_WRITE`, `SYS_LSEEK`, `SYS_DUP`, `SYS_READDIR`)
- Path/metadata syscalls (`SYS_CHDIR`, `SYS_GETCWD`, `SYS_STAT`, `SYS_FSTAT`, `SYS_LINK`, `SYS_UNLINK`, `SYS_RENAME`,
  `SYS_MKNOD`)
- Shared memory syscalls (`SYS_SHM_OPEN`, `SYS_SHM_UNLINK`)
- File descriptor syscalls (`SYS_DUP`, `SYS_DUP2`)
- PTY syscall (`SYS_OPENPTY`)

For path/metadata operations, syscall wrappers return VFS/backend status codes directly when available rather than
coalescing all backend failures to `-EIO`.
