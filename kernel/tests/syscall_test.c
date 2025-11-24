#include "test.h"
#include <stdbool.h>
#include "syscall.h"
#include "string.h"
#include "vmm.h"
#include "pmm.h"
#include "terminal.h"
#include "process.h"

// Buffer for setjmp/longjmp
static void *test_env[64];
static volatile int test_exit_code = 0;
static int test_runner_pid = 0;
static uint64_t syscall_test_rflags = 0;
static bool syscall_test_rflags_valid = false;

static inline void syscall_test_disable_interrupts(void)
{
    uint64_t flags;
    __asm__ volatile("pushf; pop %0" : "=r"(flags)::"memory");
    __asm__ volatile("cli" ::: "memory");
    syscall_test_rflags = flags;
    syscall_test_rflags_valid = true;
}

static inline void syscall_test_restore_interrupts(void)
{
    if (!syscall_test_rflags_valid)
        return;
    if (syscall_test_rflags & (1 << 9))
        __asm__ volatile("sti" ::: "memory");
    syscall_test_rflags_valid = false;
}

static void enter_user_mode(uint64_t rip, uint64_t rsp)
{
    const uint64_t user_cs = 0x20 | 3;
    const uint64_t user_ss = 0x18 | 3;
    const uint64_t rflags = 0x202;

    __asm__ volatile(
        "cli\n"
        "swapgs\n"
        "mov ds, %0\n"
        "mov es, %0\n"
        "mov fs, %0\n"
        "mov gs, %0\n"
        "push %0\n"
        "push %1\n"
        "push %2\n"
        "push %3\n"
        "push %4\n"
        "iretq\n"
        :
        : "r"(user_ss), "r"(rsp), "r"(rflags), "r"(user_cs), "r"(rip)
        : "memory", "rax", "rdx");
    __builtin_unreachable();
}

static void syscall_test_prepare_longjmp(void)
{
    syscall_test_disable_interrupts();
    __asm__ volatile("swapgs" ::: "memory");
}

static void syscall_test_resume_after_longjmp(void)
{
    __asm__ volatile("swapgs" ::: "memory");
    syscall_test_restore_interrupts();
}

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

    0x50,                         // push rax (Save PID)
    0x48, 0x83, 0xEC, 0x08,       // sub rsp, 8
    0x48, 0x89, 0xE7,             // mov rdi, rsp
    0xB8, 0x05, 0x00, 0x00, 0x00, // mov eax, 5 (SYS_WAIT)
    0x0F, 0x05,                   // syscall
    0x48, 0x83, 0xC4, 0x08,       // add rsp, 8
    0x5F,                         // pop rdi (Restore PID to EDI)

    0xB8, 0x03, 0x00, 0x00, 0x00, // mov eax, 3 (SYS_EXIT)
    0x0F, 0x05                    // syscall
};

static uint8_t fork_stub_bytes[] = {
    0xB8, 0x04, 0x00, 0x00, 0x00, // mov eax, 4 (SYS_FORK)
    0x0F, 0x05,                   // syscall
    0x83, 0xF8, 0x00,             // cmp eax, 0
    0x74, 0x34,                   // je child (offset 0x34)
    0x7C, 0x3E,                   // jl error (offset 0x3E)

    // Parent
    0x48, 0x83, 0xEC, 0x08,       // sub rsp, 8
    0x48, 0x89, 0xE7,             // mov rdi, rsp
    0xB8, 0x05, 0x00, 0x00, 0x00, // mov eax, 5 (SYS_WAIT)
    0x0F, 0x05,                   // syscall

    0x8B, 0x3C, 0x24,       // mov edi, [rsp]
    0x48, 0x83, 0xC4, 0x08, // add rsp, 8

    0x83, 0xFF, 0x64, // cmp edi, 100
    0x75, 0x0C,       // jne parent_fail (offset 0x0C)

    // Success (Parent)
    0xB8, 0x03, 0x00, 0x00, 0x00, // mov eax, 3 (SYS_EXIT)
    0xBF, 0xC8, 0x00, 0x00, 0x00, // mov edi, 200
    0x0F, 0x05,                   // syscall

    // Parent Fail
    0xB8, 0x03, 0x00, 0x00, 0x00, // mov eax, 3 (SYS_EXIT)
    0xBF, 0x01, 0x00, 0x00, 0x00, // mov edi, 1
    0x0F, 0x05,                   // syscall

    // Child
    0xB8, 0x03, 0x00, 0x00, 0x00, // mov eax, 3 (SYS_EXIT)
    0xBF, 0x64, 0x00, 0x00, 0x00, // mov edi, 100
    0x0F, 0x05,                   // syscall

    // Error
    0xB8, 0x03, 0x00, 0x00, 0x00, // mov eax, 3 (SYS_EXIT)
    0xBF, 0x02, 0x00, 0x00, 0x00, // mov edi, 2
    0x0F, 0x05                    // syscall
};

static uint8_t sbrk_stub_bytes[] = {
    0xB8, 0x09, 0x00, 0x00, 0x00, // mov eax, 9
    0x48, 0x31, 0xFF,             // xor rdi, rdi
    0x0F, 0x05,                   // syscall
    0x49, 0x89, 0xC4,             // mov r12, rax
    0xB8, 0x09, 0x00, 0x00, 0x00, // mov eax, 9
    0xBF, 0x00, 0x10, 0x00, 0x00, // mov edi, 4096
    0x0F, 0x05,                   // syscall
    0x4C, 0x39, 0xE0,             // cmp rax, r12
    0x75, 0x0C,                   // jne error (+12)
    0xC6, 0x00, 0xAA,             // mov byte ptr [rax], 0xAA
    0xB8, 0x03, 0x00, 0x00, 0x00, // mov eax, 3
    0x31, 0xFF,                   // xor edi, edi
    0x0F, 0x05,                   // syscall
    // error:
    0xB8, 0x03, 0x00, 0x00, 0x00, // mov eax, 3
    0xBF, 0x01, 0x00, 0x00, 0x00, // mov edi, 1
    0x0F, 0x05                    // syscall
};

static uint8_t file_io_stub_bytes[] = {
    0xB8, 0x0A, 0x00, 0x00, 0x00,             // mov eax, 10
    0x48, 0xC7, 0xC7, 0x00, 0x01, 0x40, 0x00, // mov rdi, 0x400100
    0x0F, 0x05,                               // syscall
    0x83, 0xF8, 0x00,                         // cmp eax, 0
    0x7C, 0x31,                               // jl error (+49)
    0x41, 0x89, 0xC4,                         // mov r12d, eax
    0xB8, 0x01, 0x00, 0x00, 0x00,             // mov eax, 1
    0x44, 0x89, 0xE7,                         // mov edi, r12d
    0x48, 0xC7, 0xC6, 0x00, 0x02, 0x40, 0x00, // mov rsi, 0x400200
    0xBA, 0x0C, 0x00, 0x00, 0x00,             // mov edx, 12
    0x0F, 0x05,                               // syscall
    0x83, 0xF8, 0x0C,                         // cmp eax, 12
    0x75, 0x13,                               // jne error (+19)
    0xB8, 0x0B, 0x00, 0x00, 0x00,             // mov eax, 11
    0x44, 0x89, 0xE7,                         // mov edi, r12d
    0x0F, 0x05,                               // syscall
    0xB8, 0x03, 0x00, 0x00, 0x00,             // mov eax, 3
    0x31, 0xFF,                               // xor edi, edi
    0x0F, 0x05,                               // syscall
    // error:
    0xB8, 0x03, 0x00, 0x00, 0x00, // mov eax, 3
    0xBF, 0x01, 0x00, 0x00, 0x00, // mov edi, 1
    0x0F, 0x05                    // syscall
};

static uint8_t chdir_stub_bytes[] = {
    0xB8, 0x0D, 0x00, 0x00, 0x00,             // mov eax, 13
    0x48, 0xC7, 0xC7, 0x00, 0x01, 0x40, 0x00, // mov rdi, 0x400100
    0x0F, 0x05,                               // syscall
    0x83, 0xF8, 0x00,                         // cmp eax, 0
    0x75, 0x07,                               // jne error (+7)
    0xB8, 0x03, 0x00, 0x00, 0x00,             // mov eax, 3
    0x31, 0xFF,                               // xor edi, edi
    0x0F, 0x05,                               // syscall
    // error:
    0xB8, 0x03, 0x00, 0x00, 0x00, // mov eax, 3
    0xBF, 0x01, 0x00, 0x00, 0x00, // mov edi, 1
    0x0F, 0x05                    // syscall
};

static uint8_t sleep_stub_bytes[] = {
    0xB8, 0x0E, 0x00, 0x00, 0x00,             // mov eax, 14
    0x48, 0xC7, 0xC7, 0x0A, 0x00, 0x00, 0x00, // mov rdi, 10
    0x0F, 0x05,                               // syscall
    0xB8, 0x03, 0x00, 0x00, 0x00,             // mov eax, 3
    0x31, 0xFF,                               // xor edi, edi
    0x0F, 0x05                                // syscall
};

static uint8_t exec_stub_bytes[] = {
    0xB8, 0x04, 0x00, 0x00, 0x00,             // mov eax, 4 (FORK)
    0x0F, 0x05,                               // syscall
    0x83, 0xF8, 0x00,                         // cmp eax, 0
    0x74, 0x31,                               // je child (+49)
    0xB8, 0x05, 0x00, 0x00, 0x00,             // mov eax, 5 (WAIT)
    0x48, 0xC7, 0xC7, 0x00, 0x02, 0x40, 0x00, // mov rdi, 0x400200
    0x0F, 0x05,                               // syscall
    0x8B, 0x04, 0x25, 0x00, 0x02, 0x40, 0x00, // mov eax, [0x400200]
    0x3D, 0x7B, 0x00, 0x00, 0x00,             // cmp eax, 123
    0x75, 0x09,                               // jne error (+9)
    0xB8, 0x03, 0x00, 0x00, 0x00,             // mov eax, 3
    0x31, 0xFF,                               // xor edi, edi
    0x0F, 0x05,                               // syscall
    // error:
    0xB8, 0x03, 0x00, 0x00, 0x00, // mov eax, 3
    0xBF, 0x01, 0x00, 0x00, 0x00, // mov edi, 1
    0x0F, 0x05,                   // syscall
    // child:
    0xB8, 0x02, 0x00, 0x00, 0x00,             // mov eax, 2 (EXEC)
    0x48, 0xC7, 0xC7, 0x00, 0x01, 0x40, 0x00, // mov rdi, 0x400100
    0x0F, 0x05,                               // syscall
    0xB8, 0x03, 0x00, 0x00, 0x00,             // mov eax, 3
    0xBF, 0x7B, 0x00, 0x00, 0x00,             // mov edi, 123
    0x0F, 0x05                                // syscall
};

static uint8_t mknod_stub_bytes[] = {
    0xB8, 0x0F, 0x00, 0x00, 0x00,             // mov eax, 15 (SYS_MKNOD)
    0x48, 0xC7, 0xC7, 0x00, 0x01, 0x40, 0x00, // mov rdi, 0x400100 (path)
    0xBE, 0x03, 0x00, 0x00, 0x00,             // mov esi, 3 (VFS_CHARDEVICE)
    0xBA, 0x01, 0x01, 0x00, 0x00,             // mov edx, 0x0101 (dev 1,1)
    0x0F, 0x05,                               // syscall
    0x89, 0xC7,                               // mov edi, eax
    0xB8, 0x03, 0x00, 0x00, 0x00,             // mov eax, 3 (SYS_EXIT)
    0x0F, 0x05                                // syscall
};

static void syscall_test_exit_handler(int code)
{
    if (test_runner_pid != 0 && current_process->pid != test_runner_pid)
    {
        return;
    }
    test_exit_code = code;
    // We caught the sys_exit from user mode!
    // Return to the test function stack
    syscall_test_prepare_longjmp();
    __builtin_longjmp(test_env, 1);
}

TEST(test_syscall_write_exit)
{
    test_runner_pid = current_process->pid;
    // 1. Allocate a page for user code and stack
    void *phys_page = pmm_alloc_page();
    if (!phys_page)
    {
        printk("Syscall Test: Failed to alloc page\n");
        return false;
    }

    // 2. Map it as User | Present | RW at 0x400000
    uint64_t user_base = 0x400000;
    uint64_t cr3;
    __asm__ volatile("mov %0, cr3" : "=r"(cr3));

    uint64_t hhdm_offset = 0xffff800000000000;

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
        // Switch to User Mode
        uint64_t user_stack = user_base + 4096 - 16; // Top of page, aligned
        enter_user_mode(user_base, user_stack);

        // Should not reach here
        printk("Syscall Test: IRETQ failed to jump\n");
        return false;
    }
    else
    {
        syscall_test_resume_after_longjmp();
        // Returned from longjmp (sys_exit handler)
        // Verify results
        bool passed = true;

        // We expect exit code 42
        if (test_exit_code != 42)
        {
            printk("Syscall Test: Exit code mismatch. Expected 42, got %d\n", test_exit_code);
            passed = false;
        }
        else
        {
            printk("Syscall Test: Write/Exit successful, exit code 42\n");
        }

        // Cleanup
        syscall_set_exit_hook(NULL);

        return passed;
    }
}

TEST(test_syscall_getpid)
{
    test_runner_pid = current_process->pid;
    // 1. Allocate a page for user code and stack
    void *phys_page = pmm_alloc_page();
    if (!phys_page)
    {
        printk("Syscall Test: Failed to alloc page\n");
        return false;
    }

    // 2. Map it as User | Present | RW at 0x400000
    uint64_t user_base = 0x400000;
    uint64_t cr3;
    __asm__ volatile("mov %0, cr3" : "=r"(cr3));

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
        // Switch to User Mode
        uint64_t user_stack = user_base + 4096 - 16;
        enter_user_mode(user_base, user_stack);

        return false;
    }
    else
    {
        syscall_test_resume_after_longjmp();
        // Returned from longjmp
        bool passed = true;

        // We expect PID 1 (kernel task)
        if (test_exit_code != current_process->pid)
        {
            printk("Syscall Test: PID mismatch. Expected %d, got %d\n", current_process->pid, test_exit_code);
            passed = false;
        }
        else
        {
            printk("Syscall Test: GETPID successful, got %d\n", test_exit_code);
        }

        // Cleanup
        syscall_set_exit_hook(NULL);

        return passed;
    }
}

TEST(test_syscall_yield)
{
    test_runner_pid = current_process->pid;
    // 1. Allocate a page for user code and stack
    void *phys_page = pmm_alloc_page();
    if (!phys_page)
    {
        printk("Syscall Test: Failed to alloc page\n");
        return false;
    }

    // 2. Map it as User | Present | RW at 0x400000
    uint64_t user_base = 0x400000;
    uint64_t cr3;
    __asm__ volatile("mov %0, cr3" : "=r"(cr3));

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
        // Switch to User Mode
        uint64_t user_stack = user_base + 4096 - 16;
        enter_user_mode(user_base, user_stack);

        return false;
    }
    else
    {
        syscall_test_resume_after_longjmp();
        // Returned from longjmp
        bool passed = true;

        // We expect exit code 0
        if (test_exit_code != 0)
        {
            printk("Syscall Test: Yield failed or invalid exit code. Got %d\n", test_exit_code);
            passed = false;
        }
        else
        {
            printk("Syscall Test: YIELD successful\n");
        }

        // Cleanup
        syscall_set_exit_hook(NULL);

        return passed;
    }
}

TEST(test_syscall_spawn)
{
    test_runner_pid = current_process->pid;
    // 1. Allocate a page for user code and stack
    void *phys_page = pmm_alloc_page();
    if (!phys_page)
    {
        printk("Syscall Test: Failed to alloc page\n");
        return false;
    }

    // 2. Map it as User | Present | RW at 0x400000
    uint64_t user_base = 0x400000;
    uint64_t cr3;
    __asm__ volatile("mov %0, cr3" : "=r"(cr3));

    uint64_t hhdm_offset = 0xffff800000000000;

    vmm_map_page((pml4_t)cr3, user_base, (uint64_t)phys_page, PTE_PRESENT | PTE_WRITABLE | PTE_USER);

    // 3. Copy stub to the page
    void *virt_page = (void *)((uint64_t)phys_page + hhdm_offset);
    memcpy(virt_page, spawn_stub_bytes, sizeof(spawn_stub_bytes));

    // Copy the path string to 0x400100
    const char *path = "/bin/prog";
    memcpy((void *)((uint64_t)virt_page + 0x100), path, strlen(path) + 1);

    // 4. Register exit hook
    syscall_set_exit_hook(syscall_test_exit_handler);

    // 5. Prepare for jump
    if (__builtin_setjmp(test_env) == 0)
    {
        // Switch to User Mode
        uint64_t user_stack = user_base + 4096 - 16;
        enter_user_mode(user_base, user_stack);

        return false;
    }
    else
    {
        syscall_test_resume_after_longjmp();
        // Returned from longjmp
        bool passed = true;

        // We expect a valid PID (> 1, since 1 is kernel)
        if (test_exit_code <= 1)
        {
            printk("Syscall Test: Spawn failed or invalid PID. Got %d\n", test_exit_code);
            passed = false;
        }
        else
        {
            printk("Syscall Test: SPAWN successful, new PID %d\n", test_exit_code);
        }

        // Cleanup
        syscall_set_exit_hook(NULL);

        return passed;
    }
}

TEST(test_syscall_fork)
{
    test_runner_pid = current_process->pid;
    // 1. Allocate a page for user code and stack
    void *phys_page = pmm_alloc_page();
    if (!phys_page)
    {
        printk("Syscall Test: Failed to alloc page\n");
        return false;
    }

    // 2. Map it as User | Present | RW at 0x400000
    uint64_t user_base = 0x400000;
    uint64_t cr3;
    __asm__ volatile("mov %0, cr3" : "=r"(cr3));

    uint64_t hhdm_offset = 0xffff800000000000;

    vmm_map_page((pml4_t)cr3, user_base, (uint64_t)phys_page, PTE_PRESENT | PTE_WRITABLE | PTE_USER);

    // 3. Copy stub to the page
    void *virt_page = (void *)((uint64_t)phys_page + hhdm_offset);
    memcpy(virt_page, fork_stub_bytes, sizeof(fork_stub_bytes));

    // 4. Register exit hook
    syscall_set_exit_hook(syscall_test_exit_handler);

    // 5. Prepare for jump
    if (__builtin_setjmp(test_env) == 0)
    {
        // Switch to User Mode
        uint64_t user_stack = user_base + 4096 - 16;
        enter_user_mode(user_base, user_stack);

        return false;
    }
    else
    {
        syscall_test_resume_after_longjmp();
        // Returned from longjmp
        bool passed = true;

        // We expect exit code 200 (Parent success)
        if (test_exit_code != 200)
        {
            printk("Syscall Test: Fork failed. Exit code %d\n", test_exit_code);
            passed = false;
        }
        else
        {
            printk("Syscall Test: FORK successful\n");
        }

        // Cleanup
        syscall_set_exit_hook(NULL);

        return passed;
    }
}

TEST(test_syscall_sbrk)
{
    test_runner_pid = current_process->pid;
    void *phys_page = pmm_alloc_page();
    if (!phys_page)
        return false;

    uint64_t user_base = 0x400000;
    uint64_t cr3;
    __asm__ volatile("mov %0, cr3" : "=r"(cr3));
    uint64_t hhdm_offset = 0xffff800000000000;
    vmm_map_page((pml4_t)cr3, user_base, (uint64_t)phys_page, PTE_PRESENT | PTE_WRITABLE | PTE_USER);

    // Initialize heap_end for sbrk test
    current_process->heap_end = 0x500000;

    void *virt_page = (void *)((uint64_t)phys_page + hhdm_offset);
    memcpy(virt_page, sbrk_stub_bytes, sizeof(sbrk_stub_bytes));

    syscall_set_exit_hook(syscall_test_exit_handler);

    if (__builtin_setjmp(test_env) == 0)
    {
        uint64_t user_stack = user_base + 4096 - 16;
        enter_user_mode(user_base, user_stack);
        return false;
    }
    else
    {
        syscall_test_resume_after_longjmp();
        bool passed = (test_exit_code == 0);
        if (passed)
            printk("Syscall Test: SBRK successful\n");
        else
            printk("Syscall Test: SBRK failed, exit code %d\n", test_exit_code);
        syscall_set_exit_hook(NULL);
        return passed;
    }
}


TEST(test_syscall_file_io)
{
    test_runner_pid = current_process->pid;
    void *phys_page = pmm_alloc_page();
    if (!phys_page)
        return false;

    uint64_t user_base = 0x400000;
    uint64_t cr3;
    __asm__ volatile("mov %0, cr3" : "=r"(cr3));
    uint64_t hhdm_offset = 0xffff800000000000;
    vmm_map_page((pml4_t)cr3, user_base, (uint64_t)phys_page, PTE_PRESENT | PTE_WRITABLE | PTE_USER);

    void *virt_page = (void *)((uint64_t)phys_page + hhdm_offset);
    memcpy(virt_page, file_io_stub_bytes, sizeof(file_io_stub_bytes));

    const char *path = "/TEST.TXT";
    memcpy((void *)((uint64_t)virt_page + 0x100), path, strlen(path) + 1);
    const char *data = "Hello FileIO";
    memcpy((void *)((uint64_t)virt_page + 0x200), data, strlen(data) + 1);

    syscall_set_exit_hook(syscall_test_exit_handler);

    if (__builtin_setjmp(test_env) == 0)
    {
        uint64_t user_stack = user_base + 4096 - 16;
        enter_user_mode(user_base, user_stack);
        return false;
    }
    else
    {
        syscall_test_resume_after_longjmp();
        bool passed = (test_exit_code == 0);
        if (passed)
            printk("Syscall Test: FileIO successful\n");
        else
            printk("Syscall Test: FileIO failed, exit code %d\n", test_exit_code);
        syscall_set_exit_hook(NULL);
        return passed;
    }
}

TEST(test_syscall_chdir)
{
    test_runner_pid = current_process->pid;
    void *phys_page = pmm_alloc_page();
    if (!phys_page)
        return false;

    uint64_t user_base = 0x400000;
    uint64_t cr3;
    __asm__ volatile("mov %0, cr3" : "=r"(cr3));
    uint64_t hhdm_offset = 0xffff800000000000;
    vmm_map_page((pml4_t)cr3, user_base, (uint64_t)phys_page, PTE_PRESENT | PTE_WRITABLE | PTE_USER);

    void *virt_page = (void *)((uint64_t)phys_page + hhdm_offset);
    memcpy(virt_page, chdir_stub_bytes, sizeof(chdir_stub_bytes));

    const char *path = "/";
    memcpy((void *)((uint64_t)virt_page + 0x100), path, strlen(path) + 1);

    syscall_set_exit_hook(syscall_test_exit_handler);

    if (__builtin_setjmp(test_env) == 0)
    {
        uint64_t user_stack = user_base + 4096 - 16;
        enter_user_mode(user_base, user_stack);
        return false;
    }
    else
    {
        syscall_test_resume_after_longjmp();
        bool passed = (test_exit_code == 0);
        if (passed)
            printk("Syscall Test: CHDIR successful\n");
        else
            printk("Syscall Test: CHDIR failed, exit code %d\n", test_exit_code);
        syscall_set_exit_hook(NULL);
        return passed;
    }
}

TEST(test_syscall_sleep)
{
    test_runner_pid = current_process->pid;
    void *phys_page = pmm_alloc_page();
    if (!phys_page)
        return false;

    uint64_t user_base = 0x400000;
    uint64_t cr3;
    __asm__ volatile("mov %0, cr3" : "=r"(cr3));
    uint64_t hhdm_offset = 0xffff800000000000;
    vmm_map_page((pml4_t)cr3, user_base, (uint64_t)phys_page, PTE_PRESENT | PTE_WRITABLE | PTE_USER);

    void *virt_page = (void *)((uint64_t)phys_page + hhdm_offset);
    memcpy(virt_page, sleep_stub_bytes, sizeof(sleep_stub_bytes));

    syscall_set_exit_hook(syscall_test_exit_handler);

    if (__builtin_setjmp(test_env) == 0)
    {
        uint64_t user_stack = user_base + 4096 - 16;
        enter_user_mode(user_base, user_stack);
        return false;
    }
    else
    {
        syscall_test_resume_after_longjmp();
        bool passed = (test_exit_code == 0);
        if (passed)
            printk("Syscall Test: SLEEP successful\n");
        else
            printk("Syscall Test: SLEEP failed, exit code %d\n", test_exit_code);
        syscall_set_exit_hook(NULL);
        return passed;
    }
}

TEST(test_syscall_exec)
{
    test_runner_pid = current_process->pid;
    void *phys_page = pmm_alloc_page();
    if (!phys_page)
        return false;

    uint64_t user_base = 0x400000;
    uint64_t cr3;
    __asm__ volatile("mov %0, cr3" : "=r"(cr3));
    uint64_t hhdm_offset = 0xffff800000000000;
    vmm_map_page((pml4_t)cr3, user_base, (uint64_t)phys_page, PTE_PRESENT | PTE_WRITABLE | PTE_USER);

    void *virt_page = (void *)((uint64_t)phys_page + hhdm_offset);
    memcpy(virt_page, exec_stub_bytes, sizeof(exec_stub_bytes));

    const char *path = "/bin/prog";
    memcpy((void *)((uint64_t)virt_page + 0x100), path, strlen(path) + 1);
    uint32_t expected_status = 123;
    memcpy((void *)((uint64_t)virt_page + 0x200), &expected_status, sizeof(uint32_t));

    syscall_set_exit_hook(syscall_test_exit_handler);

    if (__builtin_setjmp(test_env) == 0)
    {
        uint64_t user_stack = user_base + 4096 - 16;
        enter_user_mode(user_base, user_stack);
        return false;
    }
    else
    {
        syscall_test_resume_after_longjmp();
        bool passed = (test_exit_code == 0);
        if (passed)
            printk("Syscall Test: EXEC successful\n");
        else
            printk("Syscall Test: EXEC failed, exit code %d\n", test_exit_code);
        syscall_set_exit_hook(NULL);
        return passed;
    }
}

TEST_PRIO(test_syscall_mknod, 10)
{
    uint64_t user_base = 0x400000;
    test_runner_pid = current_process->pid;
    void *phys_page = pmm_alloc_page();
    if (!phys_page)
        return false;

    uint64_t cr3;
    __asm__ volatile("mov %0, cr3" : "=r"(cr3));
    cr3 &= ~0xFFF; // Mask flags

    vmm_map_page((pml4_t)cr3, user_base, (uint64_t)phys_page, PTE_PRESENT | PTE_WRITABLE | PTE_USER);

    void *virt_page = (void *)((uint64_t)phys_page + g_hhdm_offset);
    memcpy(virt_page, mknod_stub_bytes, sizeof(mknod_stub_bytes));

    const char *path = "/dev_test";
    memcpy((void *)((uint64_t)virt_page + 0x100), path, strlen(path) + 1);

    syscall_set_exit_hook(syscall_test_exit_handler);

    if (__builtin_setjmp(test_env) == 0)
    {
        uint64_t user_stack = user_base + 4096 - 16;
        enter_user_mode(user_base, user_stack);
        return false;
    }
    else
    {
        syscall_test_resume_after_longjmp();
        bool passed = (test_exit_code == 0);
        if (passed)
        {
            vfs_inode_t *node = vfs_resolve_path("/dev_test");
            if (node && (node->flags & VFS_CHARDEVICE))
            {
                printk("Syscall Test: MKNOD successful (node found)\n");
            }
            else
            {
                printk("Syscall Test: MKNOD failed (node not found or wrong type)\n");
                passed = false;
            }
        }
        else
            printk("Syscall Test: MKNOD failed, exit code %d\n", test_exit_code);
        syscall_set_exit_hook(NULL);
        return passed;
    }
}
