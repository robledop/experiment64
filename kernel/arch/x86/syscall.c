#include "syscall.h"
#include "cpu.h"
#include "gdt.h"
#include "terminal.h" // For printf
#include <stdint.h>

extern void syscall_entry(void);

// Simple single-core kernel stack for syscalls
static uint8_t kstack[4096];
uint64_t syscall_stack_top = (uint64_t)kstack + 4096;
uint64_t user_rsp_scratch;

#ifdef TEST_MODE
volatile uint64_t test_syscall_count = 0;
volatile uint64_t test_syscall_last_num = 0;
volatile uint64_t test_syscall_last_arg1 = 0;
#endif

void syscall_init(void)
{
    // Enable SCE (System Call Extensions) - Bit 0 of EFER
    uint64_t efer = rdmsr(MSR_EFER);
    efer |= 1;
    wrmsr(MSR_EFER, efer);

    // Set STAR MSR
    // Bits 63:48 - User Code Segment Base (0x10) -> CS=0x20, SS=0x18
    // Bits 47:32 - Kernel Code Segment Base (0x08) -> CS=0x08, SS=0x10
    uint64_t star = ((uint64_t)0x10 << 48) | ((uint64_t)0x08 << 32);
    wrmsr(MSR_STAR, star);

    // Set LSTAR MSR - Target RIP for syscall
    wrmsr(MSR_LSTAR, (uint64_t)syscall_entry);

    // Set SFMASK MSR - RFLAGS mask
    // Mask Interrupts (IF - bit 9)
    wrmsr(MSR_SFMASK, 0x200);

    // Set TSS RSP0 to the kernel stack
    tss_set_stack(syscall_stack_top);
}

void syscall_handler(uint64_t syscall_number, uint64_t arg1, uint64_t arg2, uint64_t arg3)
{
#ifdef TEST_MODE
    test_syscall_count++;
    test_syscall_last_num = syscall_number;
    test_syscall_last_arg1 = arg1;
#endif
    printf("Syscall called! Number: 0x%lx, Args: 0x%lx, 0x%lx, 0x%lx\n", syscall_number, arg1, arg2, arg3);
}
