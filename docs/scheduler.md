# Scheduler (`scheduler.c`, `thread.c`, `switch.S`)

The scheduler is xv6-style: each CPU runs a dedicated scheduler loop on its own
scheduler thread stack. Runnable threads live on a global process list, and the
CPU's scheduler thread selects the next runnable thread, switches into it, and
returns to the scheduler thread whenever a reschedule is needed.

---

## Key Data Structures

- `process_t`: owns a thread list and an address space (`pml4`).
- `thread_t`: thread state, kernel stack, saved context, FPU state, time slice.
- `cpu_t`: `active_thread`, `scheduler_thread`, `user_rsp`, `kernel_rsp`.

Scheduler globals:

```c
thread_t *scheduler_idle_threads[SCHEDULER_MAX_CPUS];
process_t *scheduler_rr_last_proc[SCHEDULER_MAX_CPUS]; // per-CPU round-robin state
spinlock_t scheduler_lock;
volatile uint64_t scheduler_ticks;
```

Idle threads and scheduler threads are **not** part of any process thread list.

---

## Thread States

```
THREAD_READY -> THREAD_RUNNING -> THREAD_BLOCKED -> THREAD_READY
THREAD_TERMINATED (final)
```

The scheduler validates states atomically to catch corruption and avoids running
threads that are already active on another CPU.

---

## Initialization Flow

1. `process_init()` creates the kernel process and the initial kernel thread.
2. Per-CPU scheduler threads are created and stored in `cpu->scheduler_thread`.
3. Per-CPU idle threads are created and stored in `idle_threads[]`.
4. `scheduler_ready` is set and `smp_ap_scheduler_ready()` releases APs.

APs spin in `ap_main()` until `ap_scheduler_ready`, then call
`smp_init_ap_scheduler()`. That function switches onto the scheduler thread
stack via `switch_to()` and never returns.

---

## Context Switching (`switch.S`)

`switch_to(prev, next)` saves callee-saved registers and swaps `rsp` using the
`thread_t::rsp` field:

```asm
push rbx, rbp, r12..r15
mov [rdi + THREAD_RSP_OFFSET], rsp   ; prev->rsp
mov rsp, [rsi + THREAD_RSP_OFFSET]   ; next->rsp
pop r15..rbx
ret
```

`THREAD_RSP_OFFSET` is defined in `switch.S` to match the current `thread_t` layout.

New threads start at `thread_trampoline`, which enables interrupts, calls the
thread entrypoint, and falls back to `sys_exit(0)` if the entry returns.
Scheduler threads bypass `thread_trampoline` — their `ctx->rip` is set to
`scheduler_loop` directly.

---

## Scheduler Loop

`schedule()` is called from timer interrupts, `yield()`, and sleep paths like
`thread_sleep()`, plus a few fault/exit paths that must abandon the current
context.
It disables interrupts, marks the current thread runnable when appropriate, and
switches into the per-CPU scheduler thread.

`scheduler_loop()` runs on the scheduler thread stack:

- At the top of each iteration, claims one auto-reap process (if any) via
  `scheduler_claim_auto_reap_locked()` and reaps it outside the lock before
  restarting the loop. It also collects detached terminated threads into a
  free list via `scheduler_collect_detached_terminated_threads()` for
  deferred release after the lock is dropped.
- Scans the global process list in round-robin order to avoid starvation.
- Skips threads already active on another CPU.
- Schedules user-mode and kernel threads on any CPU.
- If no runnable threads exist, falls back to the per-CPU idle thread (or
  `scheduler_idle_threads[0]` when the per-CPU slot is empty). If all idle
  threads are null, executes inline `sti; hlt; cli` and retries.

When a runnable thread is found, the scheduler:

1. Switches to the target process's `pml4`.
2. Programs the syscall stack with `syscall_set_stack(next->kstack_top)`.
3. Restores FS base (TLS pointer) and FPU state, then sets `THREAD_RUNNING`.
4. Releases `scheduler_lock`, frees any collected detached-terminated threads
   via `scheduler_release_thread_list()`, then calls `switch_to(schedt, next)`.

When the thread yields or is preempted, control returns to the scheduler thread,
which restores the kernel `pml4` and loops again.

---

## Timer Tick and Time Slicing

The timer ISR (registered at `IRQ_BASE + 0`) calls `scheduler_tick()`:

- Increments `scheduler_ticks`
- Scans all threads across all processes to validate state consistency
- Decrements `ticks_remaining` for the current thread
- On expiry: moves the thread to the tail via `scheduler_thread_list_move_to_tail()`,
  sets `THREAD_READY`, and returns `need_resched`

If `need_resched` is true, the ISR calls `schedule()` after EOI.

---

## Sleep and Wakeup

`thread_sleep(chan, lock)`:

1. Marks the current thread `THREAD_BLOCKED` and sets `chan`.
2. Releases the passed-in lock (unless it is `scheduler_lock`, which is handled
   specially), calls `schedule()`, then reacquires locks.

`thread_wakeup(chan)` scans all threads and marks matches as `THREAD_READY`. It
does not force an immediate reschedule.

Sleeplocks rely on `thread_sleep()` and therefore require the scheduler to be
initialized. They must not be acquired or released from interrupt context or
with interrupts disabled.

---

## Process Exit and Reaping

User-mode faults and `sys_exit()` mark the current thread as terminated and may
mark the owning process as terminated. Orphaned child processes are re-parented
to `init` (when present) so they can be reaped later.

Final cleanup (freeing kernel stacks/address spaces) happens when the parent
calls `wait` and the process is safe to reap.

---

## Idle Threads and Fallbacks

Idle threads are **not** part of the runnable set. When no runnable threads
exist, the scheduler switches to the per-CPU idle thread, which executes `hlt`
in a loop.

---

## SMP Interactions

- The scheduler is fully driven by per-CPU timer preemption and explicit yield/sleep paths.
- A reschedule IPI exists (`IPI_RESCHEDULE_VECTOR`), but it is not required for correctness.
- `schedule()` and `scheduler_loop()` avoid running the same thread on multiple
  CPUs at once.

---

## User Thread Syscalls (WIP)

`SYS_THREAD_CREATE` spawns a user-mode thread in the current process. The caller
provides a user entry address (`void (*entry)(void *)`) and an argument pointer
passed in `RDI`. The kernel
allocates a fixed-size user stack (currently 4 pages plus a 1-page guard, same
size as `spawn` uses) and uses an `iretq` trampoline to enter user mode.

`SYS_THREAD_EXIT` terminates the calling thread. If it is the last live thread
in the process, the process is marked terminated and the exit code is set from
the syscall argument.

`SYS_THREAD_JOIN` waits for a thread to terminate, stores its exit status to the
user pointer (if provided), and reclaims its kernel stack and thread struct.
`SYS_THREAD_DETACH` marks a thread as detached so its resources are reclaimed
without a join.

Thread syscalls return `0` or a positive value on success and negative status
codes on failure (for example `-EDEADLK`, `-EINVAL`, `-ESRCH`, `-EFAULT`).

`SYS_GETTID` returns the calling thread's TID.

Libc exposes these as `thread_create()`, `thread_exit()`, `thread_join()`,
`thread_detach()`, and `gettid()` in `user/libc/include/unistd.h`.
