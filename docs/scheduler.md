# Scheduler (`process.c`, `switch.S`)

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
static thread_t *idle_threads[MAX_CPUS];
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
mov [rdi + 16], rsp   ; prev->rsp
mov rsp, [rsi + 16]   ; next->rsp
pop r15..rbx
ret
```

New threads start at `thread_trampoline`, which enables interrupts, calls the
thread entrypoint, and falls back to `sys_exit(0)` if the entry returns.

---

## Scheduler Loop

`schedule()` is called from timer interrupts, `yield()`, and sleep paths like
`thread_sleep()`, plus a few fault/exit paths that must abandon the current
context.
It disables interrupts, marks the current thread runnable when appropriate, and
switches into the per-CPU scheduler thread.

`scheduler_loop()` runs on the scheduler thread stack:

- Scans the global process list in round-robin order to avoid starvation.
- Skips threads already active on another CPU.
- Schedules user-mode and kernel threads on any CPU.
- If no runnable threads exist, switches to the per-CPU idle thread, which halts in a loop.

When a runnable thread is found, the scheduler:

1. Switches to the target process's `pml4`.
2. Programs the syscall stack with `syscall_set_stack(next->kstack_top)`.
3. Restores FPU state and sets `THREAD_RUNNING`.
4. Releases `scheduler_lock` and `switch_to(schedt, next)`.

When the thread yields or is preempted, control returns to the scheduler thread,
which restores the kernel `pml4` and loops again.

---

## Timer Tick and Time Slicing

The LAPIC timer ISR calls `scheduler_tick()`:

- Increments `scheduler_ticks`
- Decrements `ticks_remaining` for the current thread
- Returns `need_resched` when a time slice expires

If `need_resched` is true, the ISR calls `schedule()` after EOI.

---

## Sleep and Wakeup

`thread_sleep(chan, lock)`:

1. Marks the current thread `THREAD_BLOCKED` and sets `chan`.
2. Releases locks, calls `schedule()`, then reacquires locks.

`thread_wakeup(chan)` scans all threads and marks matches as `THREAD_READY`. It
does not force an immediate reschedule.

---

## Idle Threads and Fallbacks

Idle threads are **not** part of the runnable set. They are used as safe
`active_thread` fallbacks when destroying processes or cleaning up stale thread
pointers. When no runnable threads exist, the scheduler switches to the per-CPU
idle thread, which executes `hlt` in a loop.

---

## SMP Interactions

- `thread_create()` sends `IPI_RESCHEDULE_VECTOR` to other CPUs once the
  scheduler is ready, prompting them to look for work.
- `schedule()` and `scheduler_loop()` avoid running the same thread on multiple
  CPUs at once.

---

## Current Limitations

- Global run queue (process list scan); no per-CPU run queues or priorities.
- No CPU affinity or work-stealing.
