# A syscall from the user instruction to a handler and back

This walkthrough traces what happens between a userland program executing the
`syscall` instruction and that program resuming with a return value. It follows
two concrete calls: `write()` (a simple read-only request) and `fork()` (the one
that splits the universe). The interesting part is not the dispatch table — it's
the privilege transition: the `swapgs`/stack-swap dance at the boundary, the way
the entry stub builds a `struct syscall_regs` on the kernel stack by pushing in
reverse field order, and how `fork` hand-crafts a child kernel stack so the child
later "returns" from a syscall it never executed.

## Scope

This is the x86_64 fast-path `syscall`/`sysret` flow only. It does not cover the
legacy `int 0x80` path (there isn't one), and it only details two syscalls. The
register convention, the entry stub, the dispatcher, and the user-pointer
validation are general to every syscall; `sys_write` and `sys_fork` are the
specific endpoints.

## Files in play

- `user/libc/include/sys/syscall.h` — the user-side ABI: inline `syscallN()`
  wrappers that load registers and execute `syscall`.
- `user/libc/src/syscall.c` — the libc functions (`write`, `fork`) that call
  those wrappers and translate the kernel's return value into `errno`.
- `kernel/syscalls/syscall_entry.S` — the `syscall` landing pad. Does
  `swapgs`, swaps to the kernel stack, builds `syscall_regs`, calls C, and
  returns via `sysretq`. Also holds the fork-child trampoline.
- `include/arch/x86_64/cpu.h` — defines `cpu_t`, whose field offsets (`user_rsp`
  at +8, `kernel_rsp` at +16) are what the `gs:8`/`gs:16` references in the
  entry stub resolve to.
- `include/sys/syscall.h` — defines `struct syscall_regs` and the C prototypes
  for every `sys_*` handler.
- `kernel/syscalls/syscall.c` — `syscall_init()` programs the MSRs;
  `syscall_handler()` is the C dispatcher that the entry stub calls.
- `kernel/syscalls/sys_write.c` — the `write` endpoint.
- `kernel/syscalls/common.c` — user-pointer validation (`user_ptr_read_ok` and
  friends) and the copy helpers.
- `kernel/syscalls/common_fd.c` — file-descriptor helpers (`fd_get`, `fd_put`,
  `fd_can_write`).
- `kernel/syscalls/sys_fork.c` — the `fork` endpoint; builds the child's kernel
  stack frame.
- `kernel/arch/x86_64/switch.S` — `switch_to`, the kernel context switch that
  eventually resumes the forked child.
- `kernel/task/scheduler.c` — restores per-CPU `user_rsp` before switching into
  a thread, which is the missing half of the fork return path.

## The walk

### Part 1 — `write(fd, buf, count)`

1. **User loads registers and traps.** `write()` in
   `user/libc/src/syscall.c:44` calls `syscall3(SYS_WRITE, fd, buf, count)`.
   `syscall3()` (`user/libc/include/sys/syscall.h:27`) is the convention: the
   syscall number goes in `rax` (`"a"(n)`), and the three args go in `rdi`,
   `rsi`, `rdx` (`"D"`, `"S"`, `"d"`). It then runs the bare `syscall`
   instruction. `SYS_WRITE` is `0` (`include/uapi/sys/syscall_defs.h:3`). Note
   this is *not* the SysV C calling convention's 4th-arg register — args 4/5/6
   use `r10`/`r8`/`r9` (`syscall4`/`syscall6`), because the `syscall` instruction
   itself clobbers `rcx` (it stashes the return RIP there) and `r11` (RFLAGS).
   Both are listed in the inline-asm clobber list.

2. **The CPU enters the kernel.** `syscall` jumps to whatever `MSR_LSTAR`
   points at. That was set once by `syscall_init()` to `syscall_entry`
   (`kernel/syscalls/syscall.c:48`). The same init also sets `MSR_SFMASK` to
   clear `IF|TF|DF` on entry (`syscall.c:52`), so the kernel begins with
   interrupts *disabled* — that matters for step 4. Crucially, `syscall` does
   **not** switch the stack pointer; `rsp` still points into the user stack.

3. **`swapgs` and the manual stack swap.** `syscall_entry`
   (`kernel/syscalls/syscall_entry.S:32-35`) first does `swapgs`, which exchanges
   `GS.base` with the value in `MSR_KERNEL_GS_BASE` so that `gs:` now points at
   this CPU's `cpu_t`. Then:
   ```
   mov [gs:8], rsp      # cpu->user_rsp  = rsp   (save the user stack)
   mov rsp, [gs:16]     # rsp = cpu->kernel_rsp  (switch to the kernel stack)
   ```
   Those hardcoded offsets are the layout of `cpu_t` in
   `include/arch/x86_64/cpu.h:36`: `self` at +0, `user_rsp` at +8, `kernel_rsp`
   at +16. `kernel_rsp` was loaded with the current thread's `kstack_top` by
   `syscall_set_stack()` (`syscall.c:27`) the last time the scheduler ran. This
   is why each CPU needs its own `cpu_t` and its own GS base.

4. **Build `struct syscall_regs` by pushing in reverse.** The stub now pushes
   registers (`syscall_entry.S:38-53`). Read the push order against the struct in
   `include/sys/syscall.h:16`:
   ```
   struct syscall_regs { rdi, rsi, rdx, r10, r8, r9, r15..rbp, rcx, r11 };
   ```
   The pushes go `r11, rcx, rbp, rbx, r12..r15, r9, r8, r10, rdx, rsi, rdi`.
   Because the stack grows *down*, the last thing pushed (`rdi`) ends up at the
   lowest address — i.e. `[rsp]`. So the in-memory layout, read low-to-high,
   is exactly the struct's field order. The push sequence is the struct written
   backwards. After the pushes, `rsp` *is* a valid `struct syscall_regs *`.

5. **Marshal the C arguments.** The C dispatcher's signature is
   `syscall_handler(num, a1, a2, a3, regs)` (`syscall.c:74`), so it expects
   `rdi=num, rsi=a1, rdx=a2, rcx=a3, r8=regs`. The stub shuffles
   (`syscall_entry.S:69-73`): it reads the just-saved user values back off the
   stack (`rsi = [rsp]` = old rdi = a1, etc.), puts the syscall number from
   `rax` into `rdi`, and sets `r8 = rsp` so C receives the pointer to the
   `syscall_regs` it just built. Args 4-6 are not passed as C parameters; the
   handler reaches them through `regs->r10/r8/r9` (`syscall.c:86-88`).

6. **Dispatch.** `syscall_handler` (`syscall.c:74`) first does `sti`
   (`syscall.c:78`) — interrupts were off on entry, and the kernel re-enables
   them now so blocking I/O can be preempted. It then switches on the number;
   `SYS_WRITE` calls `sys_write((int)arg1, (const char*)arg2, (size_t)arg3)`
   (`syscall.c:94`).

7. **Validate the user buffer.** `sys_write` (`kernel/syscalls/sys_write.c:7`)
   range-checks the fd, then calls `user_ptr_read_ok(buf, count, "sys_write")`
   (`sys_write.c:13`) before touching the buffer. That runs the four-step
   gauntlet in `user_ptr_access_ok` (`kernel/syscalls/common.c:101`): non-null and
   no `addr+size` overflow, both endpoints canonical, the range stays below the
   HHDM and off this thread's kernel stack, and finally a full page-table walk
   confirming every covered page is `PTE_PRESENT|PTE_USER`. Kernel threads
   (`t->is_user == false`) skip the walk and are trusted (`common.c:108-111`). A
   failure returns `-EFAULT`. This is the kernel never trusting a user pointer,
   even for a read.

8. **Resolve the fd and do the write.** `fd_get(fd)` (`common_fd.c:43`) looks up
   the descriptor under `fd_lock` and bumps its refcount atomically so it can't
   be freed mid-call; `fd_can_write` (`common_fd.c:36`) rejects `O_RDONLY`
   descriptors. The write itself is `vfs_write(...)` (`sys_write.c:28`), the
   offset advances, and `fd_put` (`common_fd.c:59`) drops the refcount — freeing
   the descriptor only if it hit zero. The byte count is returned via
   `clamp_to_int`.

9. **Late signal delivery.** Back in the dispatcher, before returning,
   `signal_deliver_after_syscall(regs, &ret)` (`syscall.c:282`) gets a chance to
   redirect the return into a signal handler — it can rewrite `regs` and `ret`.
   For an ordinary `write` with no pending signal this is a no-op.

10. **Return path.** The dispatcher's return value lands in `rax`. The stub does
    `cli` (`syscall_entry.S:78`) to close the interrupt window, then
    `syscall_return` (`syscall_entry.S:82`) pops every saved register in the
    exact reverse of the push order, restoring the user's `rcx` (return RIP) and
    `r11` (RFLAGS). Then it mirrors the entry swap:
    ```
    mov rsp, [gs:8]   # restore the user stack from cpu->user_rsp
    swapgs            # GS back to the user base
    sysretq           # jump to rcx with RFLAGS from r11, dropping to ring 3
    ```
    `sysretq` is the inverse of `syscall`: it reloads CS/SS from `MSR_STAR`'s
    user segment selectors (programmed in `syscall.c:45`) and resumes user code
    at the saved RIP. Userland's `syscall3` reads `rax`, and
    `syscall_to_ssize` (`user/libc/src/syscall.c:28`) converts a negative return
    into `errno` + `-1`.

### Part 2 — `fork()`: returning from a syscall the child never made

`fork()` in `user/libc/src/syscall.c:139` calls `syscall0(SYS_FORK)` (number
`4`). Steps 1-6 are identical — but `SYS_FORK` dispatches to `sys_fork(regs)`
(`syscall.c:103`), passing the `syscall_regs` pointer. That pointer is the whole
trick: it is a faithful snapshot of the parent's user register state at the
moment of the trap, including the user `rcx` (return RIP just past the `syscall`
instruction) and `r11` (RFLAGS).

11. **Clone the address space and process.** `sys_fork`
    (`kernel/syscalls/sys_fork.c:10`) copies the page tables with
    `vmm_copy_pml4` (`sys_fork.c:16`), creates a `process_t` (`sys_fork.c:22`),
    copies fds and VM areas, and creates a new thread with `thread_create`
    (`sys_fork.c:37`). The child's address space is a copy of the parent's, so
    the parent's user stack pointer is valid in the child too.

12. **Hand-build the child's kernel stack.** This is the heart of fork
    (`sys_fork.c:47-58`). Starting from the child's `kstack_top`, it reserves
    `KSTACK_SYSCALL_HEADROOM`, then carves out room for a `struct syscall_regs`
    *and* a `struct context`, 16-byte aligned:
    ```
    child_ctx  = (struct context*)ctx_addr;
    child_regs = (struct syscall_regs*)(ctx_addr + sizeof(struct context));
    *child_regs = *regs;                          # inherit parent's user regs
    child_ctx->rip = (uint64_t)fork_child_trampoline;
    ```
    The `syscall_regs` is placed *immediately above* the `context` on purpose
    (the comment at `sys_fork.c:44-46` says so), so that after the context
    switch unwinds the `context`, `rsp` lands exactly on `child_regs`.

13. **Why `struct context` looks the way it does.** `switch_to`
    (`kernel/arch/x86_64/switch.S`) saves `prev->rsp`, loads `next->rsp`, pops
    six callee-saved registers (`r15,r14,r13,r12,rbp,rbx`), and executes `ret`.
    That `ret` pops one more word as a return address. `struct context`
    (`include/task/process.h:40`) is laid out to feed exactly that sequence:
    `{r15,r14,r13,r12,rbp,rbx,rip}`. `sys_fork` zeroes the six saved registers
    and sets `rip = fork_child_trampoline` (`sys_fork.c:57-58`). So when the
    scheduler ever picks the child and calls `switch_to`, the final `ret`
    consumes the `rip` word and jumps to `fork_child_trampoline` — and at that
    instant `rsp == ctx_addr + sizeof(struct context)`, which is exactly where
    `child_regs` was placed in step 12.

14. **Stash the user stack pointer for the child.** `sys_fork.c:63` sets
    `child_thread->saved_user_rsp = cpu->user_rsp`. `cpu->user_rsp` is the
    parent's user `rsp` that the entry stub saved in step 3. It is copied so the
    child resumes on the same user stack address (valid in its cloned address
    space). The parent, meanwhile, returns normally through the path in step 10,
    getting the child's pid as its return value (`sys_fork.c:101`).

15. **The child gets scheduled.** Later, `scheduler_loop`
    (`kernel/task/scheduler.c:687`) restores `cpu->user_rsp =
    next->saved_user_rsp` *before* it calls `switch_to` into the child. This is
    the missing half: it re-arms `[gs:8]` with the child's user stack so the
    eventual `syscall_return` can restore it.

16. **The trampoline fakes a syscall return.** `switch_to`'s `ret` lands in
    `fork_child_trampoline` (`syscall_entry.S:105`) with `rsp == child_regs`. It
    falls straight through to `fork_return` (`syscall_entry.S:113`), which sets
    `xor rax, rax` (the child's `fork()` return value is `0`) and then
    `jmp syscall_return` — the *exact same* return code path the parent's
    `write` used in step 10. Because `rsp` already points at a populated
    `syscall_regs`, `syscall_return` pops the inherited user registers (including
    the parent's saved `rcx`/RIP and `r11`/RFLAGS), restores the user stack from
    `[gs:8]`, `swapgs`, and `sysretq` into the child. The child wakes up in
    userland at the instruction right after its `syscall`, with `rax = 0`,
    having never executed the dispatcher at all. The comment at
    `syscall_entry.S:106-109` explains why there's no `sti` here: `sysretq`
    restores `IF` from the child's saved `r11`, so enabling interrupts manually
    would just open a window with the wrong GS/RSP still active.

## Gotchas

- **The push order is the struct, reversed.** `syscall_entry.S` pushes
  `r11,rcx,...,rdi` but the stack grows down, so the resulting memory image,
  read low-to-high, matches `struct syscall_regs` field order top-to-bottom. If
  you reorder one without the other, every syscall reads garbage args. There is
  no compile-time check tying them together — it's a hand-maintained mirror.
- **`gs:8` / `gs:16` are raw struct offsets, not symbols.** They only work
  because `cpu_t` happens to place `user_rsp` at +8 and `kernel_rsp` at +16
  (`include/arch/x86_64/cpu.h:36`). Insert a field before them and the entry
  stub silently corrupts. (Contrast `switch.S`, where `THREAD_RSP_OFFSET` is
  guarded by a `static_assert`.)
- **`syscall` chooses its own arg registers.** Arg 4 is `r10`, not `rcx`,
  precisely because `syscall` overwrites `rcx` with the return address (and
  `r11` with RFLAGS). The libc wrappers know this; the C dispatcher only takes
  three args directly and fishes the rest out of `regs->r10/r8/r9`.
- **The child "returns" from code it never ran, driven by a snapshot it never
  passed.** `fork` never re-enters `syscall_handler` for the child; it forges a
  kernel-stack frame so the child's *first* execution is
  `fork_child_trampoline -> syscall_return -> sysretq`. And although the libc
  side is `syscall0(SYS_FORK)` with no arguments, the kernel side depends
  entirely on the `regs` pointer the entry stub passes in `r8` — the inherited
  register snapshot, not anything the caller supplied, is what the child
  "returns" through. The dispatcher ran exactly once, in the parent.
- **The fork return needs two cooperating files.** `sys_fork.c` saves
  `saved_user_rsp` and `scheduler.c:687` restores it into `cpu->user_rsp`. If
  you only read `sys_fork.c`, the `mov rsp, [gs:8]` in `syscall_return` looks
  like it would restore stale data — the scheduler is what makes it correct.

## See also

- `docs/syscalls.md` — the full syscall table and the dispatcher overview.
- `docs/scheduler.md` — `switch_to`, per-CPU scheduler threads, and how
  `saved_user_rsp` is managed across context switches.
- `docs/address_space.md` — kernel/user split, HHDM, kernel-stack layout
  (`KSTACK_SIZE`, `KSTACK_SYSCALL_HEADROOM`), and user-pointer validation.
- `docs/signals.md` — what `signal_deliver_after_syscall` can do to the return
  path.
- `docs/dynamic_linking.md` — the other half of process startup (how a program
  gets to the point of issuing syscalls at all).
- Acronyms (LSTAR, SFMASK, HHDM, PML4, TSS, etc.) are in `docs/glossary.md`.
