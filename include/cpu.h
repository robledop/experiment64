#pragma once

#include <stdint.h>
#include "gdt.h"

// 512-byte buffer for FXSAVE/FXRSTOR, must be 16-byte aligned
typedef struct
{
    uint8_t data[512];
} __attribute__((aligned(16))) fpu_state_t;

// MSR Constants
#define MSR_EFER 0xC0000080
#define MSR_STAR 0xC0000081
#define MSR_LSTAR 0xC0000082
#define MSR_CSTAR 0xC0000083
#define MSR_SFMASK 0xC0000084
#define MSR_FS_BASE 0xC0000100
#define MSR_GS_BASE 0xC0000101
#define MSR_KERNEL_GS_BASE 0xC0000102

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

void enable_sse(void);
void init_fpu_state(fpu_state_t *state);
void save_fpu_state(fpu_state_t *state);
void restore_fpu_state(fpu_state_t *state);
void hcf(void);

void wrmsr(uint32_t msr, uint64_t value);
uint64_t rdmsr(uint32_t msr);
