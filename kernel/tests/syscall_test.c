#define TEST_MODE
#include "test.h"
#include "syscall.h"
#include "idt.h"
#include "gdt.h"
#include "string.h"
#include "heap.h"
#include "vmm.h"
#include "pmm.h"
#include "terminal.h"
#include "process.h"

// Buffer for setjmp/longjmp
static void *test_env[64];
static volatile int test_exit_code = 0;

// Raw bytes for the stub:
// sys_exec("/user_prog.elf")
// mov eax, 2 (SYS_EXEC)
// mov edi, 0x400100
// syscall
//
// jmp $
static uint8_t user_stub_bytes[] = {
    0xB8, 0x02, 0x00, 0x00, 0x00, // mov eax, 2
    0xBF, 0x00, 0x01, 0x40, 0x00, // mov edi, 0x400100
    0x0F, 0x05,                   // syscall

    0xEB, 0xFE // jmp $
};

static uint8_t write_exit_stub_bytes[] = {
    0xB8, 0x00, 0x00, 0x00, 0x00,                               // mov eax, 0 (SYS_WRITE)
    0xBF, 0x01, 0x00, 0x00, 0x00,                               // mov edi, 1
    0x48, 0xBE, 0x00, 0x01, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, // mov rsi, 0x400100
    0xBA, 0x0C, 0x00, 0x00, 0x00,                               // mov edx, 12
    0x0F, 0x05,                                                 // syscall

    0xB8, 0x03, 0x00, 0x00, 0x00, // mov eax, 3 (SYS_EXIT)
    0xBF, 0x2A, 0x00, 0x00, 0x00, // mov edi, 42
    0x0F, 0x05                    // syscall
};

static uint8_t getpid_stub_bytes[] = {
    0xB8, 0x06, 0x00, 0x00, 0x00, // mov eax, 6 (SYS_GETPID)
    0x0F, 0x05,                   // syscall
    // RAX now has PID. Move to EDI for exit code.
    0x89, 0xC7,                   // mov edi, eax
    0xB8, 0x03, 0x00, 0x00, 0x00, // mov eax, 3 (SYS_EXIT)
    0x0F, 0x05                    // syscall
};

static uint8_t yield_stub_bytes[] = {
    0xB8, 0x07, 0x00, 0x00, 0x00, // mov eax, 7 (SYS_YIELD)
    0x0F, 0x05,                   // syscall
    // If we return, it worked. Exit with 0.
    0xBF, 0x00, 0x00, 0x00, 0x00, // mov edi, 0
    0xB8, 0x03, 0x00, 0x00, 0x00, // mov eax, 3 (SYS_EXIT)
    0x0F, 0x05                    // syscall
};

static uint8_t spawn_stub_bytes[] = {
    0xB8, 0x08, 0x00, 0x00, 0x00, // mov eax, 8 (SYS_SPAWN)
    0xBF, 0x00, 0x01, 0x40, 0x00, // mov edi, 0x400100 (path)
    0x0F, 0x05,                   // syscall
    // RAX has PID
    0x89, 0xC7,                   // mov edi, eax
    0xB8, 0x03, 0x00, 0x00, 0x00, // mov eax, 3 (SYS_EXIT)
    0x0F, 0x05                    // syscall
};

static void syscall_test_exit_handler(int code)
{
    test_exit_code = code;
    // We caught the sys_exit from user mode!
    // Return to the test function
    __builtin_longjmp(test_env, 1);
}

TEST(test_syscall_write_exit)
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
    memcpy(virt_page, write_exit_stub_bytes, sizeof(write_exit_stub_bytes));

    // Copy the string to 0x400100
    const char *msg = "Hello Write\n";
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

        // We expect exit code 42
        if (test_exit_code != 42)
        {
            printf("Syscall Test: Exit code mismatch. Expected 42, got %d\n", test_exit_code);
            passed = false;
        }
        else
        {
            printf("Syscall Test: Write/Exit successful, exit code 42\n");
        }

        // Cleanup
        syscall_set_exit_hook(NULL);

        return passed;
    }
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
    const char *path = "/prog";
    memcpy((void *)((uint64_t)virt_page + 0x100), path, strlen(path) + 1);

    // 4. Register exit hook
    syscall_set_exit_hook(syscall_test_exit_handler);

    printf("Syscall Test: Jumping to user mode...\n");

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

        // We expect exit code 123 from user_prog.elf
        if (test_exit_code != 123)
        {
            printf("Syscall Test: Exit code mismatch. Expected 123, got %d\n", test_exit_code);
            passed = false;
        }
        else
        {
            printf("Syscall Test: Exec successful, exit code 123\n");
        }

        // Cleanup
        syscall_set_exit_hook(NULL);

        // Unmap page (optional)
        // vmm_unmap_page((pml4_t)cr3, user_base);

        return passed;
    }
}

TEST(test_syscall_getpid)
{
    // 1. Allocate a page for user code and stack
    void *phys_page = pmm_alloc_page();
    if (!phys_page)
        return false;

    // 2. Map it as User | Present | RW at 0x400000
    uint64_t user_base = 0x400000;
    uint64_t cr3;
    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
    uint64_t hhdm_offset = 0xffff800000000000;

    vmm_map_page((pml4_t)cr3, user_base, (uint64_t)phys_page, PTE_PRESENT | PTE_WRITABLE | PTE_USER);

    // 3. Copy stub to the page
    void *virt_page = (void *)((uint64_t)phys_page + hhdm_offset);
    memcpy(virt_page, getpid_stub_bytes, sizeof(getpid_stub_bytes));

    // 4. Register exit hook
    syscall_set_exit_hook(syscall_test_exit_handler);

    // 5. Prepare for jump
    if (__builtin_setjmp(test_env) == 0)
    {
        uint64_t user_stack = user_base + 4096 - 16;
        uint64_t user_cs = 0x20 | 3;
        uint64_t user_ss = 0x18 | 3;
        uint64_t rflags = 0x202;

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

        return false;
    }
    else
    {
        syscall_set_exit_hook(NULL);
        // We expect exit code to be the PID.
        // Since we are running in the kernel thread (PID 1 usually, or whatever the test runner is),
        // wait, the test runner is a kernel thread.
        // Let's check what the current process PID is.
        extern process_t *current_process;
        if (test_exit_code == current_process->pid)
        {
            printf("Syscall Test: GETPID successful, got %d\n", test_exit_code);
            return true;
        }
        else
        {
            printf("Syscall Test: GETPID mismatch. Expected %d, got %d\n", current_process->pid, test_exit_code);
            return false;
        }
    }
}

TEST(test_syscall_yield)
{
    // 1. Allocate a page for user code and stack
    void *phys_page = pmm_alloc_page();
    if (!phys_page)
        return false;

    // 2. Map it as User | Present | RW at 0x400000
    uint64_t user_base = 0x400000;
    uint64_t cr3;
    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
    uint64_t hhdm_offset = 0xffff800000000000;

    vmm_map_page((pml4_t)cr3, user_base, (uint64_t)phys_page, PTE_PRESENT | PTE_WRITABLE | PTE_USER);

    // 3. Copy stub to the page
    void *virt_page = (void *)((uint64_t)phys_page + hhdm_offset);
    memcpy(virt_page, yield_stub_bytes, sizeof(yield_stub_bytes));

    // 4. Register exit hook
    syscall_set_exit_hook(syscall_test_exit_handler);

    // 5. Prepare for jump
    if (__builtin_setjmp(test_env) == 0)
    {
        uint64_t user_stack = user_base + 4096 - 16;
        uint64_t user_cs = 0x20 | 3;
        uint64_t user_ss = 0x18 | 3;
        uint64_t rflags = 0x202;

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

        return false;
    }
    else
    {
        syscall_set_exit_hook(NULL);
        if (test_exit_code == 0)
        {
            printf("Syscall Test: YIELD successful\n");
            return true;
        }
        else
        {
            printf("Syscall Test: YIELD failed, exit code %d\n", test_exit_code);
            return false;
        }
    }
}

TEST(test_syscall_spawn)
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

    uint64_t hhdm_offset = 0xffff800000000000;

    vmm_map_page((pml4_t)cr3, user_base, (uint64_t)phys_page, PTE_PRESENT | PTE_WRITABLE | PTE_USER);

    // 3. Copy stub to the page
    void *virt_page = (void *)((uint64_t)phys_page + hhdm_offset);
    memcpy(virt_page, spawn_stub_bytes, sizeof(spawn_stub_bytes));

    // Copy the path string to 0x400100
    const char *path = "/PROG";
    memcpy((void *)((uint64_t)virt_page + 0x100), path, strlen(path) + 1);

    // 4. Register exit hook
    syscall_set_exit_hook(syscall_test_exit_handler);

    // 5. Prepare for jump
    if (__builtin_setjmp(test_env) == 0)
    {
        // Switch to User Mode
        uint64_t user_stack = user_base + 4096 - 16;
        uint64_t user_cs = 0x20 | 3;
        uint64_t user_ss = 0x18 | 3;
        uint64_t rflags = 0x202;

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

        return false;
    }
    else
    {
        // Returned from longjmp
        bool passed = true;

        // We expect a valid PID (> 1, since 1 is kernel)
        if (test_exit_code <= 1)
        {
            printf("Syscall Test: Spawn failed or invalid PID. Got %d\n", test_exit_code);
            passed = false;
        }
        else
        {
            printf("Syscall Test: SPAWN successful, new PID %d\n", test_exit_code);
        }

        // Cleanup
        syscall_set_exit_hook(NULL);

        return passed;
    }
}
