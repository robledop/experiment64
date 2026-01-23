# Futexes

The kernel exposes a minimal futex pair for userland synchronization:

- `SYS_FUTEX_WAIT(uaddr, expected)` checks a 32-bit word at `uaddr`. If the
  value does not equal `expected`, it returns `-1`. Otherwise, it blocks the
  calling thread until a matching wake arrives, then returns `0`.
- `SYS_FUTEX_WAKE(uaddr, count)` wakes up to `count` threads in the current
  process waiting on `uaddr`. It returns the number of threads woken. If
  `count <= 0`, it returns `0`.

Libc exposes these as `futex_wait()` and `futex_wake()` in `user/libc/include/unistd.h`.
