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
- Signal syscalls (`SYS_SIGACTION`, `SYS_SIGRETURN`, `SYS_SIGPROCMASK`)
- Shared memory syscalls (`SYS_SHM_OPEN`, `SYS_SHM_UNLINK`)
- File descriptor syscalls (`SYS_DUP`, `SYS_DUP2`, `SYS_FCNTL`)
- PTY syscall (`SYS_OPENPTY`)
- `SYS_IOCTL` now returns `-EBADF` for bad fds, `-EFAULT` for invalid pointers, and `-ENOTTY` for unsupported
  requests/devices.

For path/metadata operations, syscall wrappers return VFS/backend status codes directly when available rather than
coalescing all backend failures to `-EIO`.

## libc Wrapper Semantics

- Kernel syscalls use the negative-status convention above.
- libc POSIX-style wrappers (`open`, `read`, `write`, `stat`, `ioctl`, `poll`, sockets, signals, etc.) now map
  failures to `-1` (or `NULL`/`MAP_FAILED`) and set `errno` to the positive status code.
- Internal/low-level APIs used by the threading layer (`thread_*`, `futex_*`, internal `sys_readdir`) still expose
  raw status returns.
- `accept()` now uses a POSIX-style `socklen_t *addrlen` value-result argument, and nonblocking listeners surface
  `EAGAIN` when no connection is pending.

## `stat` Compatibility

- `struct stat` now exposes `st_mode` and POSIX mode/type macros in `<sys/stat.h>` (`S_IF*`, `S_IS*`, permission bits).
- Kernel metadata paths (`stat`/`fstat`) populate both legacy `type` and POSIX `st_mode`.
- Legacy fields remain available for existing programs (`size`, `i_mtime`, etc.); libc also provides aliases like
  `st_size`, `st_mtime`, `st_uid`, and `st_gid`.

## `termios` Compatibility

- `<termios.h>` now exposes `TCSANOW`, `TCSADRAIN`, and `TCSAFLUSH`.
- `tcsetattr()` accepts those three action values and returns `-1` with `errno=EINVAL` for invalid actions.
- `tcsetattr()` maps actions to Linux-style `ioctl` requests (`TCSETS`, `TCSETSW`, `TCSETSF`).
- libc provides `cfmakeraw(struct termios *)` and additional POSIX-style termios constants to ease third-party ports.
