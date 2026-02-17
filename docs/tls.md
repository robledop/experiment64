# Thread-Local Storage (TLS)

Thread-local storage allows each thread to have its own copy of a variable
declared with `thread_local` (C23) or `__thread` (GCC extension).

---

## Architecture

TLS uses the x86_64 ELF Variant II model:

- The FS segment register points to a Thread Control Block (TCB).
- TLS variables live at negative offsets from FS base.
- The TCB's first field is a self-pointer (`fs:[0]` returns the TCB address).

Memory layout per thread:

```
Lower addresses                              Higher addresses
+---------+---------+--------+
|  .tbss  |  .tdata |  TCB   |
+---------+---------+--------+
                     ^
                     FS base
```

---

## Hardware: FSGSBASE

The kernel enables the FSGSBASE CPU extension (CR4 bit 16) on all CPUs during
boot. This provides unprivileged `RDFSBASE`/`WRFSBASE` instructions, so
userspace can read/write FS base without a syscall.

FSGSBASE requires Ivy Bridge or later (CPUID.7.0:EBX bit 0). The kernel panics
if the CPU lacks support.

---

## Kernel Support

### Per-thread FS base

`thread_t` contains a `uint64_t fs_base` field. The scheduler saves and
restores it on context switch using `rdfsbase`/`wrfsbase`.

### Syscall: `arch_prctl` (`SYS_ARCH_PRCTL`, 50)

```c
int arch_prctl(int code, uint64_t addr);
```

- `ARCH_SET_FS` (0x1002): set the calling thread's FS base.
- `ARCH_GET_FS` (0x1003): write the current FS base to `*addr`.

Userspace can also use `WRFSBASE` directly since FSGSBASE is enabled.

### Fork and exec

- `fork`: the child inherits the parent's `fs_base`.
- `execve`: `fs_base` is cleared to 0; the new program sets it up in `_start`.

---

## Libc Support

### Linker script (`user/user.ld`)

Defines `.tdata` and `.tbss` output sections with boundary symbols:

- `__tdata_start`, `__tdata_end`
- `__tbss_start`, `__tbss_end`

A `PT_TLS` program header is emitted so the linker can resolve TLS relocations.

### TLS initialization (`user/libc/src/tls.c`)

`__tls_init()` is called from `_start` before `main`. It:

1. Computes the TLS template size from the linker symbols.
2. Allocates a block: `[tls data | TCB]`.
3. Copies `.tdata` into the block (`.tbss` is zero-initialized by `calloc`).
4. Sets the TCB self-pointer.
5. Calls `wrfsbase` to point FS at the TCB.

`__tls_init_thread()` does the same for each new pthread.
`__tls_destroy_thread()` frees the TLS block and clears FS base. It is called
by `pthread_exit()`.

### Compiler usage

Declare thread-local variables with `thread_local` (C23):

```c
static thread_local int counter = 0;
```

The compiler generates `fs:[offset]` accesses using the Local Exec TLS model
(default for static executables).

---

## Reference files

- Kernel: `include/arch/x86_64/cpu.h`, `kernel/arch/x86_64/cpu.c`
- Scheduler: `kernel/task/scheduler.c`
- Syscall: `kernel/syscalls/sys_arch_prctl.c`
- Libc: `user/libc/src/tls.c`, `user/libc/include/tls.h`
- Linker script: `user/user.ld`
- Test: `user/tests/tls_test.c`
