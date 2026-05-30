# Boot to First Userland Process

This walkthrough follows a single thread of control from the moment Limine
hands the CPU to the kernel until `/bin/init` is running in ring 3 and forking
the shell. Every step is anchored to code that exists in this tree today; it is
not a generic "how an OS boots" article. If you read it with the files open,
you can watch one execution path cross every major subsystem the kernel sets up
during boot.

## Scope

This traces the *happy path* on the bootstrap processor (BSP):

- Limine handoff → kernel C entry (`_start`)
- Per-CPU state, GDT, IDT, APIC, TSC
- SMP application-processor (AP) bring-up
- Memory init (PMM/VMM/heap) and the scheduler bootstrap
- Boot-disk GPT scan and root mount
- Spawning PID 2 (`init`) and the ring-3 transition into `/bin/init`

It deliberately skips the internals of the ELF loader (see
`docs/dynamic_linking.md`), the slab heap (`docs/heap.md`), and the page-table
walker (`docs/vmm.md`). It also does not cover the `TEST_MODE` path, which runs
`run_tests()` instead of spawning init.

## Files in play

- `kernel/boot.c` — Limine request structs (framebuffer, HHDM, SMP) and thin accessors over the responses.
- `kernel/kernel.c` — `_start`, the single ordered list of init calls. The order here is load-bearing.
- `kernel/arch/x86_64/cpu.c` — `enable_simd()`, `enable_fsgsbase()`; low-level CPU feature gating run before anything else.
- `kernel/arch/x86_64/smp.c` — `smp_init_cpu0()` (seed BSP per-CPU state + GS base), `smp_boot_aps()`, the AP entry `ap_main()`.
- `kernel/arch/x86_64/gdt.c` — `gdt_init()` builds a per-CPU GDT + TSS and reloads segment registers.
- `kernel/arch/x86_64/idt.c` — `idt_init()` installs 256 gates and the timer/keyboard/IDE/IPI handlers; `interrupt_handler()` is the C trampoline.
- `kernel/arch/x86_64/apic.c` — `apic_init()` (MADT parse, LAPIC timer calibration, IOAPIC routing), `apic_local_init()`.
- `kernel/task/scheduler.c` — `process_init()` (kernel process + per-CPU scheduler/idle threads), `scheduler_loop()`, `schedule()`, `scheduler_tick()`.
- `kernel/task/init.c` — `process_spawn_init()` creates PID 2; `init_process_entry()` loads `/bin/init` and `iretq`s to ring 3.
- `kernel/task/thread.c` — `thread_create()` builds a kernel-stack context that lands on `thread_trampoline`.
- `kernel/arch/x86_64/switch.S` — `switch_to()` (callee-saved swap) and `thread_trampoline` (first-run entry).
- `kernel/syscalls/syscall_entry.S` — `syscall`/`sysretq` glue; the slot `sys_execve` writes to steer the return RIP.
- `kernel/fs/vfs.c` — `vfs_mount_root()`: GPT scan, boot-device selection, ext2/FAT32 mounts.
- `kernel/syscalls/sys_execve.c` — `sys_execve()` replaces an address space and arranges the next `sysretq` to land at the new entry point.
- `user/init.c` — userland PID 2: forks httpd, the window manager, and the shell.
- `user/sh.c` — the shell that `init` keeps respawning.

## The walk

1. **Limine handoff.** The kernel ELF declares request structs in dedicated
   `.requests*` sections (`kernel/boot.c:5`-`20`): a base-revision marker plus
   framebuffer, HHDM, and SMP requests. Limine fills in their `.response`
   pointers before jumping to the ELF entry point. The entry point is `_start`
   (`kernel/kernel.c:125`) — there is no assembly trampoline; control enters C
   directly in 64-bit long mode with paging already on and the higher-half
   kernel mapped (base `0xFFFFFFFF80000000`, HHDM at `0xFFFF800000000000`; see
   `docs/address_space.md`).

2. **CPU features first.** `_start` opens with `enable_simd()` then
   `enable_fsgsbase()` (`kernel/kernel.c:127`-`128`). The latter
   (`kernel/arch/x86_64/cpu.c:131`) sets `CR4.FSGSBASE` and *panics* if the CPU
   lacks the feature — the kernel uses `wrfsbase`/`rdfsbase` and direct
   `MSR_GS_BASE` writes pervasively, so this has to succeed before any per-CPU
   access. `uart_init()` then gives us a serial console for `boot_message`.

3. **Validate the Limine responses.** `boot_init()` (`kernel/boot.c:22`) halts
   (`hcf()`) if the base revision is unsupported or the HHDM response is
   missing — the HHDM offset is needed to address physical memory later.
   `boot_init_terminal()` grabs framebuffer 0 and initializes the text terminal.

4. **Seed BSP per-CPU state — *before* the GDT.** `smp_init_cpu0()`
   (`kernel/arch/x86_64/smp.c:45`) finds the entry in the Limine SMP response
   whose `lapic_id` matches `bsp_lapic_id`, fills in `cpus[i]` (index, `self`,
   etc.), loads a *null* selector into GS/FS, and writes the `cpu_t*` into both
   `MSR_GS_BASE` and `MSR_KERNEL_GS_BASE` (`smp.c:70`-`73`). **Why this must run
   before `gdt_init()`:** every per-CPU access goes through `get_cpu()`, which
   reads the GS base, and `gdt_init()` itself calls `get_cpu()` on its very
   first line. If the GS base were still zero, that read would fault or return
   garbage. The comment at `smp.c:48`-`49` spells out the dependency: without
   the SMP response there is no `cpu_t` to point GS at, so it `hcf()`s.

5. **Build the GDT/TSS and reload segments.** `gdt_init()`
   (`kernel/arch/x86_64/gdt.c:25`) writes a 7-entry per-CPU GDT (null, kernel
   code/data, user data/code, and a 16-byte TSS descriptor), `lgdt`s it, does a
   far return to reload CS to `0x08`, and reloads the data segments. Loading a
   *null* selector into GS (`gdt.c:109`) zeroes the GS base MSR as a side
   effect — so the function immediately re-writes `MSR_GS_BASE` /
   `MSR_KERNEL_GS_BASE` from the `cpu` pointer it captured at the top
   (`gdt.c:112`-`114`), then `ltr 0x28` loads the task register. This is the
   other half of the ordering constraint in step 4: GS base is established,
   transiently destroyed by the segment reload, and restored — all relying on
   the value `smp_init_cpu0` already put in place.

6. **Install interrupts.** `idt_init()` (`kernel/arch/x86_64/idt.c:416`) fills
   all 256 entries with stubs from `isr_stub_table` as ring-0 interrupt gates,
   then registers C handlers for the LAPIC timer (`IRQ_BASE+0` → `timer_isr`),
   keyboard, both IDE channels, and the reschedule IPI (`idt.c:430`-`434`). It
   finishes with `idt_reload()` and `sti` (`idt.c:436`-`437`) — interrupts are
   now globally unmasked, though no device is wired into the IOAPIC yet. All
   vectors funnel through `interrupt_handler()` (`idt.c:310`), which dispatches
   to the registered handler or, for vectors < 32 with no handler, prints a
   panic (killing the offending *user* process instead of the kernel when the
   fault came from ring 3, `idt.c:345`).

7. **Bring up the APIC + timer.** `apic_init()` (`kernel/arch/x86_64/apic.c:149`)
   disables the legacy PIC, locates the MADT (`acpi_find_table("APIC")`), maps
   the LAPIC and IOAPIC through the HHDM offset, parses interrupt source
   overrides, calibrates the LAPIC timer against the PIT
   (`apic_timer_calibrate()`, `apic.c:101`), enables the LAPIC in periodic mode
   (`apic_local_init()`, `apic.c:129`), and routes keyboard IRQ 1 to vector 33
   via the IOAPIC. From here the LAPIC timer is ticking and delivering vector 32
   — but `timer_isr` is a no-op until the scheduler is marked ready (step 11).
   `tsc_init()` follows for high-resolution timing.

8. **Start the APs.** `smp_boot_aps()` (`kernel/arch/x86_64/smp.c:85`) walks the
   SMP response again and, for every CPU that is *not* the BSP, fills in
   `cpus[i]`, sets `extra_argument = &cpus[i]`, and writes `goto_address =
   ap_main` (`smp.c:111`-`112`). Limine watches `goto_address` and releases each
   parked AP into `ap_main()` (`smp.c:18`). Each AP re-runs the per-core setup —
   `enable_simd`/`enable_fsgsbase`, writes its own GS base from
   `extra_argument`, then `gdt_init()`, `idt_reload()`, `apic_local_init()`,
   `syscall_init()` — bumps `cpus_started`, and then **spins** on
   `ap_scheduler_ready` (`smp.c:35`) because the scheduler data structures don't
   exist yet. `smp_boot_aps()` busy-waits a fixed spin count, then logs how many
   came up; it does not block on a precise count.

9. **Memory and devices.** `syscall_init()` programs the `syscall`/`sysret`
   MSRs. Then `pmm_init` / `vmm_init` / `heap_init` (`kernel/kernel.c:141`-`143`)
   come online — note these run *after* APIC/SMP setup, so anything before this
   point uses only static storage and Limine's identity/HHDM maps. The rest of
   `_start` initializes the backbuffer, keyboard, mouse, then `process_init()`,
   PCI scan, storage, the block cache, VFS, devfs, shared memory, and the
   console.

10. **Scheduler bootstrap.** `process_init()` (`kernel/task/scheduler.c:153`)
    allocates `kernel_process` as **PID 1** (`next_pid` starts at 1 in
    `kernel/task/process.c:13`), captures the current `CR3` as its PML4, and
    builds an initial kernel thread whose stack window is *derived from the
    live RSP* (`scheduler.c:198`-`203`) so the BSP's bootstrap stack passes
    later sanity checks. It then creates, per CPU, a **scheduler pseudo-thread**
    (`create_scheduler_thread`, context RIP = `scheduler_loop`, `scheduler.c:145`)
    and an **idle thread** (`create_idle_thread`). Finally it sets
    `scheduler_ready = true` and calls `smp_ap_scheduler_ready()`
    (`scheduler.c:242`-`244`), releasing every spinning AP.

11. **Per-CPU convergence on `scheduler_loop`.** This is the non-obvious part.
    The BSP never *calls* `scheduler_loop` directly. After `_start` finishes its
    setup it spawns init (step 13) and drops into a bare `hlt` loop
    (`kernel/kernel.c:164`). The next LAPIC timer tick fires `timer_isr`
    (`idt.c:160`), which now sees `scheduler_is_ready()` true, runs
    `scheduler_tick()`, and — because the running thread isn't a scheduler
    thread — returns `need_resched`, prompting `schedule()` (`idt.c:166`).
    `schedule()` (`scheduler.c:705`) saves the current thread's context and
    `switch_to(curr, scheduler_thread)`; because the scheduler thread's saved
    context RIP is `scheduler_loop`, the BSP "returns" into the loop for the
    first time. Each **AP** converges differently: released from its spin loop,
    it calls `smp_init_ap_scheduler()` (`scheduler.c:252`), which performs a
    one-way `switch_to(&bootstrap, scheduler_thread)` from a synthetic stack
    frame (`scheduler.c:274`-`279`) — needed because the AP is still on Limine's
    bootstrap stack, and saving that out-of-range RSP into the scheduler thread
    would later fail validation. Either way, **every CPU ends up parked in the
    same `scheduler_loop()`** (`scheduler.c:640`), each picking work
    round-robin under `scheduler_lock`.

12. **Mount the root filesystem.** `vfs_mount_root()` (`kernel/fs/vfs.c:714`)
    scans each present storage device for GPT partitions
    (`vfs_scan_device` → `gpt_read_partitions` with `vfs_scan_callback`), tagging
    partitions by type GUID into per-device `gpt_scan_state`. `vfs_select_boot_device`
    (`vfs.c:662`) prefers, in order, a device with both an ESP and a Linux root,
    then ESP-only, then root-only. It `ext2_mount`s the chosen root partition as
    `vfs_root` (`vfs.c:738`), falling back to a FAT32 mount at LBA 2048 if no
    ext2 root is found (`vfs.c:750`). It then overlays the data partition on
    `/mnt`, the ESP on `/boot`, and any extra root devices on `/disk1` / `/usb`.
    Without a mounted `vfs_root`, the next step's `elf_load("/bin/init")` has
    nothing to read.

13. **Spawn init (PID 2).** In the non-test build, `_start` calls
    `kernel_splash()` then `process_spawn_init()` (`kernel/kernel.c:160`-`161`).
    `process_spawn_init()` (`kernel/task/init.c:129`) creates the process named
    `"init"` (PID 2, since the kernel took PID 1), points its PML4 at the
    current `CR3`, and calls `thread_create(init_proc, init_process_entry,
    false)` (`init.c:144`). `thread_create` (`kernel/task/thread.c:13`) carves a
    context onto a fresh kernel stack whose RIP is `thread_trampoline` and whose
    `r12` holds the entry function, then marks the thread `THREAD_READY`
    (`thread.c:47`-`56`). It is now eligible for `scheduler_loop` to pick.

14. **First run of the init thread.** When a scheduler thread selects the init
    thread, `switch_to` (`kernel/arch/x86_64/switch.S:22`) restores its stack and
    `ret`s into `thread_trampoline` (`switch.S:47`), which does `sti` and
    `call r12` — i.e. calls `init_process_entry()`. That function
    (`kernel/task/init.c:29`) `elf_load`s `/bin/init`, opens `/dev/console` as
    fds 0/1/2, maps a user stack at `0x7FFFFFFFF000`, hand-builds the SysV
    initial stack (argc=0, empty argv/envp, and a full auxv including `AT_ENTRY`
    / `AT_PHDR` / `AT_BASE` for `ld.so`), and finally drops to ring 3 with a
    manual `swapgs` + `iretq` sequence (`init.c:111`-`126`), pushing user SS/RSP/
    RFLAGS/CS/RIP. Control lands at the program's entry point (or the dynamic
    interpreter's, if `/bin/init` is dynamically linked).

15. **Userland init forks the world.** `/bin/init`'s `main()` (`user/init.c:5`)
    prints its PID, forks `/bin/httpd`, forks the window manager (unless
    `HEADLESS`), and then loops forever: fork, `exec("/bin/sh")`
    (`init.c:37`-`41`), and `wait()` on children, respawning the shell whenever
    it exits. The shell (`user/sh.c`) reads commands and itself forks/execs
    programs.

16. **How exec steers the return.** Subsequent `exec`s go through `sys_execve()`
    (`kernel/syscalls/sys_execve.c:174`): it allocates a fresh PML4, `elf_load`s
    the new binary into it, builds a new user stack with argv/auxv
    (`setup_user_stack`), switches to the new PML4, and — crucially — writes the
    chosen entry point into `regs->rcx` (`sys_execve.c:266`-`267`). On the way
    back out, `syscall_entry.S` pops that saved RCX (`syscall_entry.S:95`) and
    `sysretq` (`syscall_entry.S:102`) uses RCX as the user RIP. So `execve`
    "jumps" to the new program simply by overwriting one saved register slot; no
    explicit jump is needed.

## Gotchas

- **`smp_init_cpu0()` must precede `gdt_init()`.** `gdt_init` reads `get_cpu()`
  (which dereferences the GS base) before it has restored anything, and its own
  segment reload momentarily zeroes the GS base by loading a null selector. The
  whole scheme only works because `smp_init_cpu0` planted the `cpu_t*` in
  `MSR_GS_BASE` first and `gdt_init` re-plants it at `gdt.c:112` after the reload.
- **Nobody calls `scheduler_loop()` directly.** The BSP enters it via the first
  timer interrupt → `schedule()` → `switch_to(scheduler_thread)`, because the
  scheduler pseudo-thread's saved context RIP *is* `scheduler_loop`. APs enter it
  via a one-way synthetic `switch_to` in `smp_init_ap_scheduler()`. All CPUs end
  up in the same function but arrive by two different routes.
- **PID 1 is the kernel, not init.** `next_pid` starts at 1 and
  `process_init()` consumes it for `kernel_process`, so `/bin/init` is **PID 2**.
  `getpid()` in `user/init.c` will print 2, which surprises readers expecting the
  Unix convention of init==1.
- **`execve` returns by mutating a register, not by jumping.** Setting
  `regs->rcx` to the new entry point is the entire control transfer; the magic is
  that the `syscall` ABI restores user RIP from RCX via `sysretq`. Miss the
  `regs->rcx = entry` line and the program "runs" but executes the caller's old
  code.
- **The timer ISR is armed long before it does anything.** `idt_init()` runs
  `sti` and the LAPIC timer is calibrated and periodic well before
  `process_init()`. `timer_isr`/`scheduler_tick` early-out on
  `scheduler_is_ready()` until step 10 flips the flag, so ticks are silently
  discarded during the device-init phase.
- **`init_process_entry` and `sys_execve` build *almost* the same stack twice.**
  The first ring-3 entry is hand-rolled inline in `init.c` (with a literal
  `iretq`), while every later `exec` goes through `setup_user_stack` in
  `sys_execve.c`. They construct the same SysV auxv layout but live in different
  files and use different return mechanisms (`iretq` vs `sysretq`).

## See also

- `docs/address_space.md` — higher-half layout, HHDM, user vs kernel PML4 ranges.
- `docs/scheduler.md` — the xv6-style per-CPU scheduler in depth.
- `docs/syscalls.md` — the `syscall`/`sysret` entry path and dispatcher.
- `docs/dynamic_linking.md` — what `elf_load` and the auxv actually feed `ld.so`.
- `docs/vmm.md`, `docs/pmm.md`, `docs/heap.md` — the memory subsystems init'd mid-boot.
- `docs/storage.md`, `docs/ext2.md` — the block layer and filesystem behind `vfs_mount_root`.
- Acronyms (BSP, AP, LAPIC, MADT, HHDM, GPT, TSS, auxv, …) are collected in
  `docs/glossary.md`.
