#include "test.h"
#include "syscall.h"
#include "idt.h"
#include "gdt.h"
#include "string.h"
#include "heap.h"
#include "vmm.h"
#include "pmm.h"
#include "terminal.h"

// Buffer for setjmp/longjmp
static void *test_env[64];

// Raw bytes for the stub:
// 48 B8 EF BE AD DE 00 00 00 00    mov rax, 0xDEADBEEF
// 48 BF BE BA FE CA 00 00 00 00    mov rdi, 0xCAFEBABE
// 0F 05                            syscall
// CC                               int 3
// EB FE                            jmp $
static uint8_t user_stub_bytes[] = {
    0x48, 0xB8, 0xEF, 0xBE, 0xAD, 0xDE, 0x00, 0x00, 0x00, 0x00, // mov rax, 0xDEADBEEF
    0x48, 0xBF, 0xBE, 0xBA, 0xFE, 0xCA, 0x00, 0x00, 0x00, 0x00, // mov rdi, 0xCAFEBABE
    0x0F, 0x05,                                                 // syscall
    0xCC,                                                       // int 3
    0xEB, 0xFE                                                  // jmp $
};

static void syscall_test_bp_handler(struct interrupt_frame *frame)
{
    (void)frame;
    // We caught the int 3 from user mode!
    // Return to the test function
    __builtin_longjmp(test_env, 1);
}

TEST(test_syscall_instruction)
{
    // 1. Allocate a page for user code and stack
    void *phys_page = pmm_alloc_page();
    if (!phys_page)
    {
        printf("Syscall Test: Failed to alloc page\n");
        return false;
    }

    // 2. Map it as User | Present | RW at 0x400000
    uint64_t user_base = 0x400000;
    uint64_t cr3;
    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));

    // We assume HHDM offset is 0xffff800000000000 for accessing the page to copy code
    uint64_t hhdm_offset = 0xffff800000000000;

    // Map the page
    vmm_map_page((pml4_t)cr3, user_base, (uint64_t)phys_page, PTE_PRESENT | PTE_WRITABLE | PTE_USER);

    // 3. Copy stub to the page
    void *virt_page = (void *)((uint64_t)phys_page + hhdm_offset);
    memcpy(virt_page, user_stub_bytes, sizeof(user_stub_bytes));

    // 4. Register int 3 handler
    // We must use register_trap_handler to allow Ring 3 access (DPL=3)
    register_trap_handler(3, syscall_test_bp_handler);

    // 5. Prepare for jump
    if (__builtin_setjmp(test_env) == 0)
    {
        // Initial state
        test_syscall_count = 0;
        test_syscall_last_num = 0;
        test_syscall_last_arg1 = 0;

        // Switch to User Mode
        // Stack frame for IRETQ:
        // SS
        // RSP
        // RFLAGS
        // CS
        // RIP

        uint64_t user_stack = user_base + 4096 - 16; // Top of page, aligned
        uint64_t user_cs = 0x20 | 3;                 // User Code Selector (Ring 3)
        uint64_t user_ss = 0x18 | 3;                 // User Data Selector (Ring 3)
        uint64_t rflags = 0x202;                     // Interrupts enabled (IF=1), Reserved(1)=1

        __asm__ volatile(
            "mov %0, %%ds\n"
            "mov %0, %%es\n"
            "mov %0, %%fs\n"
            "mov %0, %%gs\n"
            "pushq %0\n" // SS
            "pushq %1\n" // RSP
            "pushq %2\n" // RFLAGS
            "pushq %3\n" // CS
            "pushq %4\n" // RIP
            "iretq\n"
            :
            : "r"(user_ss), "r"(user_stack), "r"(rflags), "r"(user_cs), "r"(user_base)
            : "memory");

        // Should not reach here
        printf("Syscall Test: IRETQ failed to jump\n");
        return false;
    }
    else
    {
        // Returned from longjmp (int 3 handler)

        // Verify results
        bool passed = true;
        if (test_syscall_count != 1)
        {
            printf("Syscall Test: Count mismatch. Expected 1, got %lu\n", test_syscall_count);
            passed = false;
        }

        if (test_syscall_last_num != 0xDEADBEEF)
        {
            printf("Syscall Test: Syscall number mismatch. Expected 0xDEADBEEF, got 0x%lx\n", test_syscall_last_num);
            passed = false;
        }

        if (test_syscall_last_arg1 != 0xCAFEBABE)
        {
            printf("Syscall Test: Arg1 mismatch. Expected 0xCAFEBABE, got 0x%lx\n", test_syscall_last_arg1);
            passed = false;
        }

        // Cleanup
        register_interrupt_handler(3, NULL);
        vmm_unmap_page((pml4_t)cr3, user_base);

        return passed;
    }
}
