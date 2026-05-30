# Installing, raising, and delivering a signal

This walkthrough traces a signal from `sigaction()` installing a handler, through
`kill()` marking it pending, to the handler actually running in user space and
returning cleanly to the interrupted code. The interesting part is the
**user-stack trampoline dance**: the kernel never "calls" the handler. It forges
a stack frame so that returning to user mode lands in the handler, and the
handler's own `ret` falls into a libc trampoline that asks the kernel to undo
everything. If you have only seen the textbook "the kernel runs your handler"
description, the mechanics here will surprise you.

## Scope

- Traces the catchable path: install a handler, `kill` it pending, deliver it on
  the next return to user mode, run it, `sigreturn` back.
- Covers both delivery points (after a syscall, after a hardware interrupt) and
  notes where they differ.
- Signals are **process-wide**, represented as a `sigset_t` bitset; repeated
  deliveries of the same signal coalesce into one pending bit.
- Simplifications: no stop/continue state (`SIGKILL`/`SIGSTOP` just terminate),
  `SA_RESTART` is not implemented, `sigsuspend` is a stub, and `kill(pid, 0)` is
  not a liveness check. See `docs/signals.md` for the full disposition rules.

## Files in play

- `user/libc/src/signal.c` — the `sigaction()`/`signal()` wrappers; they always
  install libc's restorer trampoline as `sa_restorer`.
- `user/libc/src/signal_trampoline.S` — `__signal_trampoline`, the restorer that
  issues `SYS_SIGRETURN`.
- `kernel/syscalls/sys_sigaction.c` — stores a disposition into the process.
- `kernel/syscalls/sys_kill.c` — thin wrapper over `signal_send_pid`.
- `kernel/task/signal.c` — the core: send, claim-pending, build the user frame,
  and the two delivery functions.
- `kernel/syscalls/syscall.c` / `kernel/arch/x86_64/idt.c` — the two delivery
  *points* (return-from-syscall, return-from-interrupt).
- `kernel/syscalls/sys_sigreturn.c` — restores the pre-signal register state.
- `include/sys/signal.h` — `sigaction_t` and `sigcontext_t` layouts; `include/task/process.h:81-86` holds the per-process `sigactions`/`sig_mask`/`sig_pending`/`sig_inflight`.

## The walk

1. **Install a handler.** A program calls `sigaction(signum, &act, &old)`
   (`user/libc/src/signal.c:29`). The wrapper copies the caller's struct and
   **forces `local.sa_restorer = __signal_trampoline`** (`signal.c:36`) — user
   code never sets the restorer itself — then issues `SYS_SIGACTION`. In the
   kernel, `sys_sigaction` (`kernel/syscalls/sys_sigaction.c`) stores the
   disposition into `current_process->sigactions[signum - 1]`
   (`sys_sigaction.c:34`). `SIGKILL`/`SIGSTOP` cannot be overridden, and a real
   handler with no `sa_restorer` is rejected (`sys_sigaction.c:30`).

2. **Raise the signal.** Something calls `kill(pid, sig)` →`SYS_KILL` →
   `sys_kill` (`kernel/syscalls/sys_kill.c:3`) → `signal_send_pid`
   (`kernel/task/signal.c:177`). Under `scheduler_lock` it finds the target,
   refuses to signal PID ≤ 1 or `init` (`signal.c:201`), then decides by
   disposition (`signal.c:213-224`):
   - uncatchable, or default-action-is-terminate with no handler →
     `signal_terminate_locked` right now (`signal.c:214`);
   - ignored / default-ignore → clear the pending bit and return;
   - otherwise → **set the pending bit** `sig_pending |= signal_bit(sig)` and, if
     the signal isn't masked, call `signal_mark_threads_ready` (`signal.c:219-223`)
     to flip any `THREAD_BLOCKED` thread back to `THREAD_READY` so it can run and
     receive the signal. Sending does *not* run the handler; it only arms it.

3. **Nothing happens until the target returns to user mode.** Signals are never
   delivered while in kernel code. There are exactly two delivery points, both
   on the way back to ring 3:
   - after a syscall, `signal_deliver_after_syscall(regs, &ret)` is called at the
     tail of the dispatcher (`kernel/syscalls/syscall.c:282`), just before the
     return value flows back to `syscall_entry.S`;
   - after a hardware interrupt that came from user mode,
     `signal_deliver_after_interrupt(frame)` is called for any IRQ vector
     (`kernel/arch/x86_64/idt.c:409-411`), just before `iretq`.

4. **Claim the highest-priority pending signal.** Both delivery functions take
   `scheduler_lock`, bail if `sig_inflight` is already set (one handler at a
   time), then call `signal_claim_pending_locked` (`signal.c:98`). That walks
   `sig_pending & ~sig_mask` from signal 1 upward — **lowest number wins** — skips
   ignored dispositions, clears the chosen bit, and returns the signal plus its
   `sigaction_t` (`signal.c:105-127`). If a claimed signal turns out to be
   terminating, the process is killed here instead (`signal.c:290` / `:419`).

5. **Block the signal and mark it in-flight.** Before building the frame, the
   kernel sets `sig_mask = old_mask | action.sa_mask | bit` and
   `sig_inflight = sig` (`signal.c:320-322`, interrupt path `:451-453`). This is
   why a handler does not re-enter itself, and why `sa_mask` signals stay blocked
   for the duration — both are undone by `sigreturn`.

6. **Snapshot the interrupted state into a `sigcontext_t`.** The kernel fills a
   `sigcontext_t` (`include/sys/signal.h:79`) from the saved registers
   (`signal.c:329-348`). Two subtleties on the **syscall** path: `ctx.rax` is set
   to the syscall's *real* return value `*ret` (so it can be restored later), and
   `ctx.rip`/`ctx.rflags` come from `regs->rcx`/`regs->r11` — because that is
   where `syscall`/`sysret` stash the user RIP and RFLAGS (see walkthrough 03).
   The interrupt path copies straight from the `interrupt_frame`
   (`signal.c:457-476`).

7. **Forge the user stack frame.** `signal_setup_user_frame` (`signal.c:131`)
   takes the current user RSP and lays down, from high address to low:

   ```
   high  ┌─────────────────────┐  <- original user RSP
         │   sigcontext_t       │  (the snapshot from step 6)
         ├─────────────────────┤  <- sigctx_addr (16-byte aligned)
         │   sa_restorer addr   │  (8 bytes)
   low   └─────────────────────┘  <- new RSP  (returned as out_rsp)
   ```

   It `copy_to_user`s the snapshot and then the restorer address
   (`signal.c:146-149`). The restorer address sits exactly where a `ret`
   instruction will look for a return address.

8. **Redirect the return into the handler.** The kernel rewrites the saved state
   so the imminent `sysretq`/`iretq` enters the handler instead of the
   interrupted instruction: RIP → `sa_handler`, RDI → `sig` (the handler's `int`
   argument), RSP → the forged `new_rsp`. On the syscall path this is
   `regs->rcx = handler; regs->rdi = sig` plus `cpu->user_rsp`/`saved_user_rsp`
   (`signal.c:368-372`); on the interrupt path it writes `frame->rip/rdi/rsp`
   directly (`signal.c:498-503`).

9. **The handler runs in user mode.** Control returns to ring 3 at the handler,
   which looks like an ordinary `void handler(int sig)` call: `sig` is in RDI,
   and `[RSP]` holds the restorer address as if it were the caller's return
   address.

10. **The handler returns into the trampoline.** When the handler executes `ret`,
    it pops that restorer address and jumps to `__signal_trampoline`
    (`user/libc/src/signal_trampoline.S:5`). RSP now points exactly at the
    `sigcontext_t`. The trampoline does `mov rdi, rsp; mov rax, SYS_SIGRETURN;
    syscall` (`signal_trampoline.S:6-8`) — handing the kernel a pointer to the
    snapshot.

11. **`sigreturn` rewinds everything.** `sys_sigreturn` (`kernel/syscalls/sys_sigreturn.c:7`)
    copies the `sigcontext_t` back in, rejects a non-canonical saved RIP
    (`sys_sigreturn.c:18`, or `sysret` would `#GP` in ring 0), restores
    `sig_mask` and clears `sig_inflight` (`sys_sigreturn.c:23-26`), reloads the
    general-purpose registers, sets `regs->rcx = ctx.rip` and
    `regs->r11 = ctx.rflags` (the `sysret` RIP/RFLAGS slots), restores user RSP,
    and **returns `ctx.rax`** (`sys_sigreturn.c:51`). Because `sigreturn` is
    itself a syscall, that return value becomes the new RAX — which is how the
    *original* syscall's result (saved in step 6) survives the detour.

After `sigreturn`, the interrupted code resumes exactly where it left off, with
its registers, RFLAGS, stack, and signal mask intact — as if the handler had been
spliced in and removed without a trace.

## Gotchas

- **The kernel never calls the handler; it returns into it.** Delivery is a
  forged stack frame plus a rewritten saved RIP. The whole mechanism rides the
  normal `sysretq`/`iretq` return path — there is no special "invoke handler"
  jump.

- **The handler's return address is a trampoline, not real code.** A handler
  that `ret`s falls into `__signal_trampoline`, which calls `SYS_SIGRETURN`. This
  is why libc force-installs `sa_restorer` (step 1) and why a handler without one
  is fatal — **and the two delivery paths disagree on how fatal**: the syscall
  path kills the process with `SIGSEGV` (`signal.c:305-318`), the interrupt path
  `panic`s the kernel (`signal.c:439-440`).

- **`sig_inflight` serializes handlers.** Only one handler runs at a time; a
  second deliverable signal waits until `sigreturn` clears `sig_inflight`. The
  current signal plus `sa_mask` are blocked meanwhile and restored by `sigreturn`.

- **Two near-duplicate delivery functions.** `signal_deliver_after_syscall` and
  `signal_deliver_after_interrupt` are almost identical; they differ only because
  one reads/writes a `struct syscall_regs` and the other an `interrupt_frame`,
  and because the syscall path has to preserve the syscall's return value in
  `ctx.rax`.

- **Lowest signal number wins, and masked signals stay pending.** Selection is
  `sig_pending & ~sig_mask` scanned upward (`signal.c:105-106`); a blocked signal
  simply waits in `sig_pending` until unmasked.

- **Signals are process-wide.** The pending set, mask, and dispositions live on
  `process_t`, not per-thread; whichever thread next returns to user mode and
  finds a deliverable signal runs the handler.

## See also

- `docs/signals.md` — dispositions, default actions, masks, and inheritance
  across `fork`/`execve` in full.
- `docs/walkthroughs/03-syscall-roundtrip.md` — why RIP/RFLAGS live in RCX/R11,
  and the `sysretq` path this piggybacks on.
- `docs/walkthroughs/02-timer-tick-to-context-switch.md` — the interrupt-return
  path that hosts the second delivery point.
- Acronyms (RIP, RFLAGS, RSP, IRQ) are defined in `docs/glossary.md`.
