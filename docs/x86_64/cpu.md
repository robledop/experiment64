# CPU Management (`cpu.c`)

This file handles CPU-specific functionality including SIMD/FPU state management, MSR access, and per-CPU data structures.

---

## Key Concepts

### SIMD (Single Instruction, Multiple Data)
Modern x86 CPUs have vector extensions that allow processing multiple values in parallel:

| Extension | Register Size | Introduced   |
|-----------|---------------|--------------|
| SSE       | 128-bit (XMM) | Pentium III  |
| AVX       | 256-bit (YMM) | Sandy Bridge |
| AVX-512   | 512-bit (ZMM) | Skylake-X    |

These must be explicitly enabled by the OS before user programs can use them.

### FPU State
When context switching between threads, the CPU's floating-point/SIMD registers must be saved and restored. This includes:
- **x87 FPU registers** (legacy floating-point)
- **SSE registers** (XMM0-XMM15)
- **AVX registers** (YMM0-YMM15, if enabled)

### XSAVE vs FXSAVE
Two mechanisms exist for saving FPU/SIMD state:

| Instruction | Size                  | Features                            |
|-------------|-----------------------|-------------------------------------|
| `FXSAVE`    | 512 bytes             | x87 + SSE only                      |
| `XSAVE`     | Variable (up to 2KB+) | x87 + SSE + AVX + future extensions |

`XSAVE` is preferred on modern CPUs as it's extensible and can be optimized with `XSAVEOPT`.

---

## Global State Variables

```c
static bool g_use_xsave = false;      // CPU supports XSAVE?
static bool g_use_xsaveopt = false;   // CPU supports XSAVEOPT (optimized)?
static bool g_avx_enabled = false;    // AVX is enabled and usable?
static uint64_t g_xsave_mask;         // Which state components to save (XCR0)
static uint32_t g_fpu_save_size;      // Size of save area in bytes
```

---

## Functions

### `enable_simd()` — Enable SIMD Extensions
Called once per CPU core during initialization.

**Steps:**
1. **Configure CR0:**
   - Clear `EM` (Emulation) — don't trap FPU instructions
   - Set `MP` (Monitor Coprocessor)
   - Clear `TS` (Task Switched) — don't trap on FPU use

2. **Configure CR4:**
   - Set `OSFXSR` — enable FXSAVE/FXRSTOR
   - Set `OSXMMEXCPT` — enable SSE exceptions
   - Set `OSXSAVE` — enable XSAVE (if supported)

3. **Detect CPU features via CPUID:**
   - Check for XSAVE support (CPUID.1:ECX bit 26)
   - Check for AVX support (CPUID.1:ECX bit 28)

4. **Configure XCR0 (Extended Control Register 0):**
   - Enable x87, SSE, and AVX state components
   - Query required save area size

5. **Initialize FPU state:**
   - `FNINIT` — reset x87 FPU
   - `LDMXCSR` — set SSE control register (mask all exceptions)

---

### `save_fpu_state(fpu_state_t* state)` — Save FPU/SIMD Registers
Called during context switch to save outgoing thread's state.

```c
if (g_use_xsave) {
    if (g_use_xsaveopt)
        xsaveopt(state);  // Only saves modified components
    else
        xsave(state);
} else {
    fxsave(state);        // Legacy 512-byte save
}
```

---

### `restore_fpu_state(fpu_state_t* state)` — Restore FPU/SIMD Registers
Called during context switch to restore incoming thread's state.

```c
if (g_use_xsave)
    xrstor(state);
else
    fxrstor(state);
```

---

### `init_fpu_state(fpu_state_t* state)` — Initialize Clean FPU State
Sets up a fresh FPU state for a new thread.

**Initializes:**
- `FCW` (x87 Control Word) at offset 0 -> `0x037F` (extended precision, exceptions masked)
- `MXCSR` (SSE Control) at offset 24 -> `0x1F80` (all exceptions masked)
- XSAVE header at offset 512 (if using XSAVE)

---

### `wrmsr(msr, value)` / `rdmsr(msr)` — MSR Access
Read/write Model-Specific Registers.

Common MSRs used:

| MSR          | Name             | Purpose                   |
|--------------|------------------|---------------------------|
| `0xC0000101` | `GS_BASE`        | Per-CPU data pointer      |
| `0xC0000102` | `KERNEL_GS_BASE` | Swapped on syscall entry  |
| `0xC0000080` | `EFER`           | Extended Feature Enable   |
| `0xC0000081` | `STAR`           | Syscall segment selectors |
| `0xC0000082` | `LSTAR`          | Syscall entry point       |

---

### `get_cpu()` — Get Current CPU Structure
Returns pointer to the current CPU's `cpu_t` structure.

```c
cpu_t* get_cpu(void) {
    cpu_t* cpu;
    __asm__ volatile("mov %0, gs:[0]" : "=r"(cpu));
    return cpu;
}
```

**How it works:**
- `GS` segment base is set to point to the CPU's `cpu_t` structure
- First field of `cpu_t` is a self-pointer (`cpu_t* self`)
- Reading `gs:[0]` retrieves this pointer

This is a common pattern for per-CPU data in kernels.

---

### `hcf()` — Halt and Catch Fire
Permanently halts the CPU (used for unrecoverable errors).

```c
void hcf(void) {
    cli();           // Disable interrupts
    for (;;) hlt();  // Halt forever
}
```

---

### `cpu_is_hypervisor()` — Detect Virtualization
Checks if running inside a VM (CPUID.1:ECX bit 31).

---

### `cpu_has_avx()` / `cpu_fpu_save_size()` — Query CPU Capabilities
Helper functions to check AVX availability and FPU save area size.

---

## XCR0 Bit Definitions

| Bit | Name       | Description                  |
|-----|------------|------------------------------|
| 0   | `XCR0_X87` | x87 FPU state                |
| 1   | `XCR0_SSE` | SSE state (XMM registers)    |
| 2   | `XCR0_AVX` | AVX state (upper YMM halves) |

---

## Context Switch Flow

```
Thread A running
Timer interrupt (scheduler tick)
save_fpu_state(&thread_a->fpu_state)
Switch to Thread B's page table & stack
restore_fpu_state(&thread_b->fpu_state)
Thread B running
```

This ensures each thread sees its own FPU/SIMD register values.

