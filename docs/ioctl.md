# Ioctl Support

The kernel only accepts a small set of ioctl requests. Any other request
returns `-ENOTTY` (libc wrapper: `-1` with `errno=ENOTTY`).

## /dev/console

- `TIOCGWINSZ`: writes a `struct winsize` to the user pointer.
- `TIOCGETA`: writes a `struct termios` to the user pointer.
- `TIOCSETA`: reads a `struct termios` from the user pointer.
- `TCGETS`/`TCSETS`/`TCSETSW`/`TCSETSF` are accepted as Linux-compatible aliases (`TCSETS*` currently behave like immediate apply).
- `TIOCGPGRP`: writes an `int` foreground PID to the user pointer.
- `TIOCSPGRP`: reads an `int` foreground PID from the user pointer.
- `FIONBIO`: reads an `int`; non-zero enables non-blocking reads.
- Reads consume console input from keyboard and serial (COM1).

## PTY fds (from `openpty`)

- `TIOCGWINSZ`: writes a `struct winsize` to the user pointer.
- `TIOCSWINSZ`: reads a `struct winsize` from the user pointer and updates the PTY size.
- `TIOCGETA`: writes a `struct termios` to the user pointer.
- `TIOCSETA`: reads a `struct termios` from the user pointer.
- `TCGETS`/`TCSETS`/`TCSETSW`/`TCSETSF` are accepted as Linux-compatible aliases (`TCSETS*` currently behave like immediate apply).
- `TIOCGPGRP`: writes an `int` foreground PID to the user pointer (works on both master and slave).
- `TIOCSPGRP`: reads an `int` foreground PID from the user pointer (slave side only).
- `FIONBIO`: reads an `int`; non-zero enables non-blocking reads on that endpoint.

## /dev/fb0

- `FB_IOCTL_GET_WIDTH`: writes a `uint32_t` width.
- `FB_IOCTL_GET_HEIGHT`: writes a `uint32_t` height.
- `FB_IOCTL_GET_PITCH`: writes a `uint32_t` pitch.
- `FB_IOCTL_GET_FBADDR`: writes a `uint64_t` framebuffer address.

## /dev/keyboard

- `KDFLUSH`: flushes keyboard buffers. The argument is ignored.

## /dev/eth0

- `GETNETINFO`: writes a `struct netinfo` with MAC, IP, subnet mask,
  default gateway, and DNS server.
