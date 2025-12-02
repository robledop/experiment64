#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "gdt.h"

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
#define MSR_EFER 0xC0000080
#define MSR_STAR 0xC0000081
#define MSR_LSTAR 0xC0000082
#define MSR_CSTAR 0xC0000083
#define MSR_SFMASK 0xC0000084
#define MSR_FS_BASE 0xC0000100
#define MSR_GS_BASE 0xC0000101
#define MSR_KERNEL_GS_BASE 0xC0000102

#define RFLAGS_IF 0x200

struct Thread;

typedef struct cpu
{
    struct cpu *self;
    uint64_t user_rsp;
    uint64_t kernel_rsp;
    struct Thread *active_thread;
    int lapic_id;
    struct gdt_desc gdt[7];
    struct tss_entry tss;
} cpu_t;

cpu_t *get_cpu(void);

[[noreturn]] void hcf(void);
void wrmsr(uint32_t msr, uint64_t value);
uint64_t rdmsr(uint32_t msr);
void enable_simd(void);
void init_fpu_state(fpu_state_t *state);
void save_fpu_state(fpu_state_t *state);
void restore_fpu_state(fpu_state_t *state);
bool cpu_has_avx(void);
uint32_t cpu_fpu_save_size(void);
bool cpu_is_hypervisor(void);

static inline uint64_t rdtsc(void)
{
    uint32_t low, high;
    __asm__ volatile("rdtsc" : "=a"(low), "=d"(high));
    return ((uint64_t)high << 32) | low;
}
