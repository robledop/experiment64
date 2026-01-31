# Signals

https://www.youtube.com/watch?v=d0gS5TXarXc&t=1009s

---

## Supported signal set

The signal numbers are defined in `include/sys/signal.h` and
`user/libc/include/sys/signal.h`. `SIG_MAX` is 32, and the implemented set is
the traditional POSIX signals from `SIGHUP` through `SIGTTOU`.

Signals are process-wide, not per-thread, and are represented as a bitset
(`sigset_t`). Multiple deliveries of the same signal coalesce into a single
pending bit.

---

## Default actions

- `SIGCHLD` and `SIGCONT` are ignored by default.
- `SIGKILL` and `SIGSTOP` are uncatchable and currently terminate the target
  process (there is no stop/continue state yet).
- All other signals terminate the process by default.

Termination sets the process exit code to `128 + signum`, marks the process and
all of its threads as terminated, and re-parents children to `init` when
available.

---

## Delivery points

Signals are delivered only when returning to user mode:

- After a syscall completes, just before `sysretq`.
- After a hardware interrupt handler completes, just before `iretq` (user mode
  only).

Signals are not delivered while running in kernel mode, and they are not
delivered during exception handling. Signals are sourced from `kill`, the
console keyboard (Ctrl+C queues `SIGINT` for the console foreground PID set
via `TIOCSPGRP`, or the current user process when no foreground is set), and
child termination (`SIGCHLD` is generated when a child exits or is terminated
by a signal). `SIGPIPE` is not generated yet.

When a signal is sent to a process with blocked threads, the kernel marks any
blocked threads as runnable so that one can return to user mode and receive the
signal. For `SIGCHLD`, the pending bit is only set (and threads marked runnable)
when the parent has a non-default, non-ignored handler installed; otherwise the
pending bit is cleared.

Signals are delivered in ascending numeric order (the lowest-numbered pending
signal wins).

---

## Dispositions and masks

Each process owns:

- `sigactions[SIG_MAX]` for handler state.
- `sig_mask` for blocked signals.
- `sig_pending` for pending signals.
- `sig_inflight` to prevent nested handlers.

Handler installation uses `sigaction`:

- `SIGKILL` and `SIGSTOP` cannot be overridden (attempts fail).
- `SIG_IGN` clears any pending instance of that signal.
- `SA_NOCLDWAIT` on `SIGCHLD` enables auto-reap: exited children do not become
  zombies and `wait` returns `-1` for the parent.
- Setting `SIGCHLD` to `SIG_IGN` also enables auto-reap.
- Other `sa_flags` are stored but currently ignored (`SA_RESTART` is not implemented).

When a handler runs, the kernel sets the mask to:

```
old_mask | sa_mask | (1 << (signum - 1))
```

This blocks the current signal and any signals listed in `sa_mask` until
`sigreturn` completes.

Inheritance rules:

- `fork` copies dispositions and the current mask. Pending signals are cleared.
- `execve` resets dispositions to `SIG_DFL` unless they were `SIG_IGN`, and
  clears masks and pending bits.

---

## User-mode delivery mechanics

When delivering a signal, the kernel:

1. Builds a `sigcontext_t` snapshot of the user registers and the syscall
   return value (if the signal arrived after a syscall).
2. Writes the `sigcontext_t` onto the user stack.
3. Pushes the `sa_restorer` address on the user stack as the return address.
4. Sets `RIP` to the handler and `RDI` to the signal number.

The libc `sigaction()` wrapper always installs a restorer trampoline, so user
code should not set `sa_restorer` manually.

---

## Returning from a handler

The restorer trampoline issues `SYS_SIGRETURN` with a pointer to the
`sigcontext_t` on the user stack. The kernel restores:

- General-purpose registers
- `RIP`, `RFLAGS`, and `RSP`
- The previous signal mask
- The saved syscall return value (`sigcontext_t.rax`)

After `sigreturn`, execution resumes at the interrupted instruction as if the
signal handler had not interposed, except that the handler ran first.

---

## User-space API

Libc provides:

- `sigaction(int signum, const struct sigaction *act, struct sigaction *oldact)`
- `signal(int signum, sighandler_t handler)`
- `kill(int pid, int sig)`

Handlers use the signature:

```
void handler(int sig);
```

`kill(pid, 0)` is a liveness check: it returns 0 if the PID exists, otherwise
-1.

---

## Example program

See `user/sigaction_demo.c` for a minimal user-space example. Build userland,
run `sigaction_demo`, and then send `SIGTERM` or `SIGUSR1` with `kill` to observe
handler delivery.
