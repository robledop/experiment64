#include "cpu.h"

void enable_sse(void)
{
    uint64_t cr0;
    uint64_t cr4;

    __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
    cr0 &= ~(1 << 2); // Clear EM (Emulation)
    cr0 |= (1 << 1);  // Set MP (Monitor Co-processor)
    cr0 &= ~(1 << 3); // Clear TS (Task Switched)
    __asm__ volatile("mov %0, %%cr0" ::"r"(cr0));

    __asm__ volatile("mov %%cr4, %0" : "=r"(cr4));
    cr4 |= (1 << 9);  // Set OSFXSR (OS Support for FXSAVE/FXRSTOR)
    cr4 |= (1 << 10); // Set OSXMMEXCPT (OS Support for Unmasked SIMD Floating-Point Exceptions)
    __asm__ volatile("mov %0, %%cr4" ::"r"(cr4));
}

void save_fpu_state(fpu_state_t *state)
{
    __asm__ volatile("fxsave %0" : "=m"(*state));
}

void restore_fpu_state(fpu_state_t *state)
{
    __asm__ volatile("fxrstor %0" : : "m"(*state));
}

void hcf(void)
{
    for (;;)
    {
        __asm__("hlt");
    }
}
