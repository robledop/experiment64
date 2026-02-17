# Networking and Socket Syscalls

The kernel networking stack currently supports IPv4 with UDP, TCP (listen/accept path), and ICMP.

Socket-related syscalls:

- `SYS_SOCKET`
- `SYS_BIND`
- `SYS_LISTEN`
- `SYS_ACCEPT`
- `SYS_SENDTO`
- `SYS_RECVFROM`

Type/protocol combinations accepted by `SYS_SOCKET`:

- `SOCK_STREAM` + `IPPROTO_TCP`
- `SOCK_DGRAM` + `IPPROTO_UDP`
- `SOCK_RAW` + `IPPROTO_ICMP`

When protocol is `0`, the kernel defaults to:

- `SOCK_STREAM -> IPPROTO_TCP`
- `SOCK_DGRAM -> IPPROTO_UDP`
- `SOCK_RAW -> IPPROTO_ICMP`

Error return convention for these syscalls:

- Success returns `0` or a positive value (for example, an fd or byte count).
- Failure returns a negative status code (for example `-EBADF`, `-EINVAL`,
  `-EFAULT`, `-ENOTSUP`, `-EAGAIN`, `-ENOMEM`).

Notable behavior:

- `SYS_RECVFROM` with `MSG_DONTWAIT` returns `-EAGAIN` when no packet is queued.
- Pointer validation failures return `-EFAULT`.
