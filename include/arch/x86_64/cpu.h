#pragma once

#include <stdint.h>
#include <arch/x86_64/gdt.h>
#include <attributes.h>

// XCR0 feature bits
#define XCR0_X87 (1u << 0)
#define XCR0_SSE (1u << 1)
#define XCR0_AVX (1u << 2)

// Buffer for XSAVE/FXSAVE state (x87 + SSE + AVX); 64-byte aligned for XSAVE
#define FPU_STATE_SIZE 1024

typedef struct
{
    uint8_t data[FPU_STATE_SIZE];
} __attribute__((aligned(64))) fpu_state_t;

// MSR Constants
#define MSR_EFER 0xC0000080             // Extended Feature Enable Register
#define MSR_STAR 0xC0000081             // Syscall Target Address Register
#define MSR_LSTAR 0xC0000082            // Long Mode Syscall Target Address
#define MSR_CSTAR 0xC0000083            // Compatibility Mode Syscall Target Address
#define MSR_SFMASK 0xC0000084           // Syscall Flags Mask
#define MSR_FS_BASE 0xC0000100          // FS Base Address
#define MSR_GS_BASE 0xC0000101          // GS Base Address
#define MSR_KERNEL_GS_BASE 0xC0000102   // Kernel GS Base Address

#define RFLAGS_TF 0x100                // Trap Flag
#define RFLAGS_IF 0x200                // Interrupt Enable Flag
#define RFLAGS_DF 0x400                // Direction Flag

struct Thread;

typedef struct cpu
{
    // self / user_rsp / kernel_rsp are addressed by HARDCODED byte offset
    // (gs:[0], gs:8, gs:16) from the swapgs paths in
    // kernel/syscalls/syscall_entry.S; do not reorder without updating those.
    struct cpu* self;
    uint64_t user_rsp;
    uint64_t kernel_rsp;
    struct Thread* active_thread;
    struct Thread* scheduler_thread; // Per-CPU scheduler context (xv6-style)
    int lapic_id;
    int cpu_index; // Index into cpus[] array (0 = BSP)
    struct gdt_desc gdt[7];
    struct tss_entry tss;
    uint32_t interrupt_depth;
} cpu_t;

// Reads gs:[0] (cpu_t.self). The GS base is established in three phases during
// bring-up: smp_init_cpu0()/ap_main() (kernel/arch/x86_64/smp.c) first
// wrmsr(MSR_GS_BASE, cpu), then gdt_init() (kernel/arch/x86_64/gdt.c) re-writes
// it because loading a null GS data selector there zeroes the GS-base MSR.
cpu_t* get_cpu(void);
void cpu_interrupt_enter(void);
void cpu_interrupt_exit(void);
bool cpu_in_interrupt(void);

[[noreturn]] void hcf(void);
void wrmsr(uint32_t msr, uint64_t value);
uint64_t rdmsr(uint32_t msr);
void enable_simd(void);
void enable_fsgsbase(void);
void init_fpu_state(fpu_state_t* state);
void save_fpu_state(fpu_state_t* state);
void restore_fpu_state(fpu_state_t* state);
bool cpu_has_avx(void);
uint32_t cpu_fpu_save_size(void);
bool cpu_is_hypervisor(void);

USED static inline uint64_t rdfsbase(void)
{
    uint64_t val;
    __asm__ volatile ("rdfsbase %0" : "=r"(val));
    return val;
}

USED static inline void wrfsbase(uint64_t val)
{
    __asm__ volatile ("wrfsbase %0" :: "r"(val));
}

USED static inline uint64_t rdtsc(void)
{
    uint32_t low, high;
    __asm__ volatile("rdtsc" : "=a"(low), "=d"(high));
    return ((uint64_t)high << 32) | low;
}
