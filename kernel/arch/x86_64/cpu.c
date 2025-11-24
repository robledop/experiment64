#include "cpu.h"

void enable_sse(void)
{
    uint64_t cr0;
    uint64_t cr4;

    __asm__ volatile("mov %0, cr0" : "=r"(cr0));
    cr0 &= ~(1 << 2); // Clear EM (Emulation)
    cr0 |= (1 << 1);  // Set MP (Monitor Co-processor)
    cr0 &= ~(1 << 3); // Clear TS (Task Switched)
    __asm__ volatile("mov cr0, %0" ::"r"(cr0));

    __asm__ volatile("mov %0, cr4" : "=r"(cr4));
    cr4 |= (1 << 9);  // Set OSFXSR (OS Support for FXSAVE/FXRSTOR)
    cr4 |= (1 << 10); // Set OSXMMEXCPT (OS Support for Unmasked SIMD Floating-Point Exceptions)
    __asm__ volatile("mov cr4, %0" ::"r"(cr4));

    __asm__ volatile("fninit");
    uint32_t mxcsr = 0x1F80; // Mask all exceptions
    __asm__ volatile("ldmxcsr %0" ::"m"(mxcsr));
}

void save_fpu_state(fpu_state_t *state)
{
    __asm__ volatile("fxsave %0" : "=m"(*state));
}

void restore_fpu_state(fpu_state_t *state)
{
    __asm__ volatile("fxrstor %0" : : "m"(*state));
}

void wrmsr(uint32_t msr, uint64_t value)
{
    uint32_t low = value & 0xFFFFFFFF;
    uint32_t high = value >> 32;
    __asm__ volatile("wrmsr" : : "c"(msr), "a"(low), "d"(high));
}

uint64_t rdmsr(uint32_t msr)
{
    uint32_t low, high;
    __asm__ volatile("rdmsr" : "=a"(low), "=d"(high) : "c"(msr));
    return ((uint64_t)high << 32) | low;
}

cpu_t *get_cpu(void)
{
    cpu_t *cpu;
    __asm__ volatile("mov %0, gs:[0]" : "=r"(cpu));
    return cpu;
}

void hcf(void)
{
    __asm__ volatile("cli");
    for (;;)
    {
        __asm__("hlt");
    }
}

void init_fpu_state(fpu_state_t *state)
{
    for (int i = 0; i < 512; i++)
    {
        state->data[i] = 0;
    }

    // Set MXCSR to default (0x1F80) - All exceptions masked
    // MXCSR is at offset 24
    uint32_t *mxcsr = (uint32_t *)&state->data[24];
    *mxcsr = 0x1F80;

    // Set FCW to default (0x037F) - Extended precision, all exceptions masked
    uint16_t *fcw = (uint16_t *)&state->data[0];
    *fcw = 0x037F;
}
