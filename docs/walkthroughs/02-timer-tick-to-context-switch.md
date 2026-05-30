# A Timer Tick to a Context Switch

This walkthrough traces what happens when a PIT timer interrupt preempts a
running thread and the CPU ends up executing a *different* thread. The
interesting part is not the interrupt itself but the control-flow inversion in
the middle: this kernel never switches directly from the outgoing thread to the
incoming one. It bounces through a per-CPU **scheduler pseudo-thread**, so a
single preemption is two context switches (`current -> scheduler -> next`). If
you have only ever read a "scheduler picks next, jumps to it" description, the
shape here will surprise you.

## Scope

- Traces the **timer preemption** path: PIT IRQ -> `timer_isr` -> `schedule()`
  -> `scheduler_loop` -> `switch_to` -> the next thread.
- `schedule()` is the shared chokepoint. It is also reached from the
  cross-CPU reschedule IPI (`reschedule_ipi_handler`, `idt.c:194`) and from
  voluntary block/yield paths. Those entries are not traced here; everything
  downstream of `schedule()` is identical.
- Single-tick mechanics only. Process creation, signals, and reaping are
  mentioned where the path touches them, but not walked.
- xv6-style design: one scheduler thread per CPU, global round-robin selection,
  50 ms slices. No priorities beyond a test-ordering knob, no run queues per
  CPU (selection scans the global process list).

## Files in play

- `kernel/arch/x86_64/interrupts.S` — assembly ISR stubs; saves GPRs, does
  `swapgs` on ring transitions, calls `interrupt_handler`.
- `kernel/arch/x86_64/idt.c` — `interrupt_handler` dispatch and `timer_isr`,
  the IRQ0 handler that decides whether to reschedule.
- `kernel/task/scheduler.c` — `scheduler_tick`, `schedule`, `scheduler_loop`,
  the round-robin policy (`find_any_runnable_thread_rr`), and validation. The
  scheduling-policy helpers that used to live in `scheduler_checks.c` are now
  all here.
- `kernel/arch/x86_64/switch.S` — `switch_to` (the actual stack swap) and
  `thread_trampoline` (where brand-new threads begin).
- `kernel/task/thread.c` — `thread_create`, which seeds a new thread's stack so
  its first `switch_to` "returns" into `thread_trampoline`.
- `include/task/process.h` — `struct context` and the `static_assert` that pins
  `thread_t.rsp` to the hardcoded offset `switch.S` relies on.

## The walk

1. **The hardware fires IRQ0 and the stub runs.** The PIT raises the timer
   vector (`IRQ_BASE + 0`, registered at `idt.c:430`). The CPU vectors through
   the IDT into the per-vector stub, which jumps to `isr_common_stub`
   (`interrupts.S:37`). The stub conditionally `swapgs`-es if it interrupted
   ring 3 (`test byte ptr [rsp + 24], 3`), pushes all GPRs, and calls
   `interrupt_handler` with `rdi = rsp` (`interrupts.S:58-59`). Note: this runs
   on the current thread's *kernel* stack, with the GPRs of the interrupted
   code now sitting on that stack.

2. **`interrupt_handler` dispatches to `timer_isr`.** `interrupt_handler`
   (`idt.c:310`) bumps the per-CPU interrupt depth with `cpu_interrupt_enter()`
   (`idt.c:312`), then calls the registered handler — here `timer_isr`
   (`idt.c:313-314`). It balances the depth with `cpu_interrupt_exit()` at its
   tail (`idt.c:413`).

3. **`scheduler_tick` decides whether to preempt.** `timer_isr` (`idt.c:160`)
   calls `scheduler_tick()` (`scheduler.c:366`). This bumps the global
   `scheduler_ticks`, then under `scheduler_lock` finds the current thread via
   `cpu->active_thread` (`scheduler.c:395`). If the current thread is itself the
   scheduler thread, it returns `false` immediately (`scheduler.c:397-400`) —
   the scheduler thread is never preempted. Otherwise, for a normal RUNNING
   thread it decrements `ticks_remaining` and, when it hits zero, marks the
   thread `THREAD_READY`, moves it to the tail of its process's thread list for
   fairness, and sets `need_resched = true` (`scheduler.c:434-442`). *Why the
   `else` branch matters:* an idle thread (or a thread not in RUNNING state)
   takes the `else` at `scheduler.c:443-445` and unconditionally sets
   `need_resched = true`, so the idle thread yields the CPU on the very next
   tick if anything became runnable.

4. **`timer_isr` calls `schedule()` — with the EOI already sent.** Back in
   `timer_isr` (`idt.c:162-168`), the EOI is sent (`apic_send_eoi()`) *before*
   the reschedule so the interrupt is acknowledged regardless of where the
   switch lands. If `need_resched`, it brackets `schedule()` with
   `cpu_interrupt_exit()` / `cpu_interrupt_enter()` (`idt.c:165-167`). *Why:* the
   stack swap inside `schedule()` lands on a different thread's stack, which has
   its own pending unwind through `interrupt_handler`'s eventual
   `cpu_interrupt_exit`. Dropping the depth across the switch keeps the per-CPU
   `interrupt_depth` accounting consistent for whichever thread is switched in.

5. **`schedule()` saves the outgoing thread and switches to the scheduler
   thread.** `schedule()` (`scheduler.c:705`) first snapshots RFLAGS and disables
   interrupts (`pushfq; pop; cli`, `scheduler.c:709`). It re-reads `curr` and
   `scheduler_thread` from the CPU. If we are preempting a still-RUNNING,
   non-idle thread, it marks it `THREAD_READY` and rotates it to the list tail
   (`scheduler.c:724-727`) — redundant with the tick on the slice-expiry path,
   but required when `schedule()` is entered from the other callers. It then
   saves the user RSP, FS base, and FPU state into the thread struct
   (`scheduler.c:729-731`), releases `scheduler_lock`, and calls
   `switch_to(curr, scheduler_thread)` (`scheduler.c:733-734`). **Read that
   ordering twice: the lock is released *before* the switch, not held across
   it.**

6. **`switch_to` swaps stacks. This is the inversion.** `switch_to`
   (`switch.S:22`) pushes the six callee-saved registers, stores `rsp` into
   `prev->rsp` at `THREAD_RSP_OFFSET` (1032), loads `rsp` from `next->rsp`,
   pops six callee-saved registers, and `ret`s (`switch.S:24-45`). The pushed
   registers plus the return address form exactly a `struct context`
   (`process.h:40`: `r15, r14, r13, r12, rbp, rbx, rip`). The crucial fact:
   after `mov rsp, [rsi + THREAD_RSP_OFFSET]`, the `pop`s and `ret` read the
   *next* thread's saved frame. So the `ret` does not return to `schedule()`'s
   caller — it returns wherever the scheduler thread last parked. The outgoing
   `schedule()` is now frozen mid-call, its continuation saved on its own stack.

7. **Execution resumes inside `scheduler_loop`.** The scheduler thread was last
   parked just after its own `switch_to(scheduler_thread, next)` at
   `scheduler.c:697`. So step 6's `ret` lands at `scheduler.c:699`: it switches
   the page tables back to the kernel's (`vmm_switch_pml4(kernel_process->pml4)`)
   and sets `active_thread` back to the scheduler thread, then loops
   (`scheduler.c:699-702`). *Why this works:* `scheduler_loop` (`scheduler.c:640`)
   is an infinite `for(;;)` whose body always ends in a `switch_to` to some
   thread; every time control re-enters the loop it is "returning" from that
   switch as if the running thread had just called back into the scheduler.

8. **The scheduler picks the next thread.** Inside the loop body
   (`scheduler.c:654`) it acquires `scheduler_lock`, opportunistically reaps
   auto-reap children and detached terminated threads, then calls
   `scheduler_find_next_thread_locked(cpu)` (`scheduler.c:667`). That defers to
   `find_any_runnable_thread_rr` (`scheduler.c:505`), which walks the global
   `process_list` starting *after* this CPU's last-served process
   (`scheduler_rr_last_proc[cpu_idx]`, `scheduler.c:516-517`), wrapping around,
   and returns the first thread that is `THREAD_READY`, non-idle, and **not
   already active on any CPU** (`thread_is_active_on_any_cpu`,
   `scheduler.c:525`). If nothing is runnable, it falls back to this CPU's idle
   thread (`scheduler.c:554-560`). *Why the per-CPU cursor plus the
   active-on-any-CPU guard:* selection is one global pool, so the cursor gives
   round-robin fairness and the guard prevents two CPUs from running the same
   thread at once.

9. **The next thread is validated before being trusted.** `scheduler_loop`
   calls `scheduler_validate_next_thread_locked` (`scheduler.c:563`). It checks
   `next->rsp` lies inside `[kstack_top - KSTACK_SIZE, kstack_top)`, and that
   the saved RIP — read from `next->rsp + 6 * sizeof(uint64_t)` — is non-zero
   and in the higher half (`scheduler.c:586-604`). *Why `6 * 8`:* that offset is
   the `rip` field of the saved `struct context` (six 8-byte callee-saved slots
   precede it), i.e. the exact address `switch_to`'s final `ret` will pop. A
   thread that fails validation is marked `THREAD_TERMINATED` and skipped rather
   than jumped into.

10. **The scheduler installs the next thread's CPU context, then switches.**
    Still in the loop (`scheduler.c:681-693`): if the target has its own address
    space it loads `next->process->pml4`, programs the syscall/TSS kernel stack
    via `syscall_set_stack(next->kstack_top)`, restores `cpu->user_rsp`, the FS
    base (`wrfsbase`), and the FPU state, sets `active_thread = next`, marks it
    RUNNING, and refills `ticks_remaining`. It **releases `scheduler_lock`**
    (`scheduler.c:695`) and only then calls `switch_to(scheduler_thread, next)`
    (`scheduler.c:697`).

11. **`switch_to` lands on the next thread — two outcomes.** This is the second
    `switch_to` of the preemption. Where its `ret` goes depends on what the next
    thread last saved:
    - **A previously-preempted thread** parked at `schedule()`'s line after its
      own `switch_to`, so it resumes at `scheduler.c:736`: it restores RFLAGS
      (re-enabling interrupts only if they were on when it was preempted) and
      returns up through `timer_isr` and the ISR stub, which `iretq`s back to
      exactly where this thread was interrupted. Note RFLAGS was captured at
      *that thread's* `schedule()` entry (`scheduler.c:709`) and is restored
      here — interrupt-enable state is preserved per thread across the bounce.
    - **A brand-new thread** has never run, so its saved frame was hand-built by
      `thread_create` (`thread.c:44-51`): `ctx->rip = thread_trampoline` and
      `ctx->r12 = entry`. The `ret` therefore lands at `thread_trampoline`
      (`switch.S:48`), which does `sti` (new threads start with interrupts
      disabled, inherited from the `cli` path), `call r12` to enter the thread's
      C entry point, and if that ever returns, falls through to `sys_exit(0)`
      (`switch.S:52-58`).

In both cases the CPU is now executing the next thread, on the next thread's
kernel stack, with the scheduler thread frozen back at `scheduler.c:697`,
waiting for the next preemption to bounce control back to it.

## Gotchas

- **`switch_to` is entered by one thread and exited by another.** A thread
  "calls" `switch_to` on the way out and "returns" from a *different*
  `switch_to` call on the way back in. The function's `ret` reads the incoming
  thread's stack, not the caller's. Reading `switch.S` linearly hides this; the
  asymmetry only appears when you track which `rsp` is live at the `ret`.

- **The lock is released before the switch, in both directions.**
  `scheduler_lock` is dropped at `scheduler.c:733` (in `schedule`) and
  `scheduler.c:695` (in `scheduler_loop`) *before* the corresponding
  `switch_to`. It is never held while a normal thread runs. Most readers expect
  the lock to protect the switch itself; here it protects only selection and
  bookkeeping.

- **There is no direct thread-to-thread switch.** Every preemption is
  `current -> scheduler thread -> next`: two `switch_to` calls through the
  per-CPU scheduler pseudo-thread (`cpu->scheduler_thread`). The scheduler is a
  real thread with a real stack, created by `create_scheduler_thread`
  (`scheduler.c:115`), not just a function.

- **`THREAD_RSP_OFFSET` is a magic number duplicated in assembly.** `switch.S`
  hardcodes `1032` for `thread_t.rsp`. The `static_assert` at `process.h:129`
  is the only thing that catches a struct-layout change — and it fails the build
  rather than corrupting switches at runtime. The `6 * 8` RIP-slot offset in
  `scheduler_validate_next_thread_locked` is the same ABI assumption in C form.

- **`timer_isr` brackets `schedule()` with interrupt-depth bookkeeping.** The
  `cpu_interrupt_exit()` / `cpu_interrupt_enter()` around `schedule()` at
  `idt.c:165-167` look like dead motion until you notice the switch lands on a
  different stack whose own `interrupt_handler` unwind (`idt.c:413`) must see a
  balanced per-CPU `interrupt_depth`.

## See also

- `docs/scheduler.md` — the scheduler design in full (states, reaping, SMP).
- `docs/address_space.md` — kernel stacks, `KSTACK_SYSCALL_HEADROOM`, TSS RSP0.
- `docs/syscalls.md` — the other path that saves/restores user RSP and FS base.
- Acronyms (PIT, EOI, ISR, IDT, TSS, HHDM, PML4, RR) are defined in
  `docs/glossary.md`.
