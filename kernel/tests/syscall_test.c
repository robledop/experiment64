#ifdef TEST_MODE
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
// sys_write(1, msg, 19)
// mov eax, 1
// mov edi, 1
// mov rsi, 0x400100
// mov edx, 19
// syscall
//
// sys_exit(42)
// mov eax, 60
// mov edi, 42
// syscall
//
// jmp $
static uint8_t user_stub_bytes[] = {
    0xB8, 0x01, 0x00, 0x00, 0x00,                               // mov eax, 1
    0xBF, 0x01, 0x00, 0x00, 0x00,                               // mov edi, 1
    0x48, 0xBE, 0x00, 0x01, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, // mov rsi, 0x400100
    0xBA, 0x13, 0x00, 0x00, 0x00,                               // mov edx, 19
    0x0F, 0x05,                                                 // syscall

    0xB8, 0x3C, 0x00, 0x00, 0x00, // mov eax, 60
    0xBF, 0x2A, 0x00, 0x00, 0x00, // mov edi, 42
    0x0F, 0x05,                   // syscall

    0xEB, 0xFE // jmp $
};

static void syscall_test_exit_handler(int code)
{
    (void)code;
    // We caught the sys_exit from user mode!
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

    // Copy the string to 0x400100
    const char *msg = "Hello from Ring 3!\n";
    memcpy((void *)((uint64_t)virt_page + 0x100), msg, strlen(msg) + 1);

    // 4. Register exit hook
    syscall_set_exit_hook(syscall_test_exit_handler);

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
        // Returned from longjmp (sys_exit handler)

        // Verify results
        bool passed = true;
        // We expect 2 syscalls: write and exit
        if (test_syscall_count != 2)
        {
            printf("Syscall Test: Count mismatch. Expected 2, got %lu\n", test_syscall_count);
            passed = false;
        }

        // Last syscall should be SYS_EXIT (60)
        if (test_syscall_last_num != SYS_EXIT)
        {
            printf("Syscall Test: Last syscall mismatch. Expected %d, got %lu\n", SYS_EXIT, test_syscall_last_num);
            passed = false;
        }

        // Last arg1 should be 42
        if (test_syscall_last_arg1 != 42)
        {
            printf("Syscall Test: Exit code mismatch. Expected 42, got %lu\n", test_syscall_last_arg1);
            passed = false;
        }

        // Cleanup
        syscall_set_exit_hook(NULL);
        vmm_unmap_page((pml4_t)cr3, user_base);

        return passed;
    }
}
#endif // TEST_MODE