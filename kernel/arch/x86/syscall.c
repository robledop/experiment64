#define TEST_MODE
#include "syscall.h"
#include "cpu.h"
#include "gdt.h"
#include "terminal.h"
#include "elf.h"
#include "vmm.h"
#include "pmm.h"
#include "process.h"
#include "keyboard.h"
#include "uart.h"
#include <stdint.h>

extern void syscall_entry(void);

// Current kernel stack for syscalls (updated on context switch)
// Initialized to a temporary stack until scheduler takes over
static uint8_t bootstrap_stack[4096];
uint64_t syscall_stack_top = (uint64_t)bootstrap_stack + 4096;
uint64_t user_rsp_scratch;

void syscall_set_stack(uint64_t stack_top)
{
    syscall_stack_top = stack_top;
    tss_set_stack(stack_top);
}

struct syscall_regs
{
    uint64_t rdi, rsi, rdx, r10, r8, r9;
    uint64_t r15, r14, r13, r12, rbx, rbp;
    uint64_t rcx, r11;
};

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

static void (*exit_hook)(int) = NULL;

void syscall_set_exit_hook(void (*hook)(int))
{
    exit_hook = hook;
}

void sys_write(int fd, const char *buf, size_t count)
{
    if (fd == 1 || fd == 2) // stdout or stderr
    {
        terminal_write(buf, count);
    }
}

void sys_exit(int code)
{
    if (exit_hook)
    {
        exit_hook(code);
    }
    printf("Process %d exited with code %d\n", current_process->pid, code);
    current_process->exit_code = code;
    current_process->terminated = true;
    current_thread->state = THREAD_TERMINATED;
    // Save current thread state
    thread_t *current = get_current_thread();
    if (current)
    {
        // Save context if needed (already saved by interrupt handler)
    }

    schedule();
}

void spawn_trampoline(void)
{
    uint64_t user_cs = 0x20 | 3;
    uint64_t user_ss = 0x18 | 3;
    uint64_t rflags = 0x202;

    __asm__ volatile(
        "mov %0, %%ds\n"
        "mov %0, %%es\n"
        "mov %0, %%fs\n"
        "mov %0, %%gs\n"
        "pushq %1\n" // SS
        "pushq %2\n" // RSP
        "pushq %3\n" // RFLAGS
        "pushq %4\n" // CS
        "pushq %5\n" // RIP
        "iretq\n"
        :
        : "r"(user_ss), "r"(user_ss), "r"(current_thread->user_stack), "r"(rflags), "r"(user_cs), "r"(current_thread->user_entry)
        : "memory");
}

int sys_spawn(const char *path)
{
    pml4_t new_pml4 = vmm_new_pml4();
    if (!new_pml4)
        return -1;

    uint64_t entry_point;
    if (!elf_load(path, &entry_point, new_pml4))
    {
        return -1;
    }

    uint64_t stack_top = 0x7FFFFFFFF000;
    uint64_t stack_size = 4 * 4096;
    uint64_t stack_base = stack_top - stack_size;

    for (uint64_t addr = stack_base; addr < stack_top; addr += 4096)
    {
        void *phys = pmm_alloc_page();
        vmm_map_page(new_pml4, addr, (uint64_t)phys, PTE_PRESENT | PTE_WRITABLE | PTE_USER);
    }

    process_t *proc = process_create(path);
    proc->pml4 = new_pml4;
    proc->parent = current_process;

    thread_t *thread = thread_create(proc, spawn_trampoline, false);
    thread->user_entry = entry_point;
    thread->user_stack = stack_top;

    return proc->pid;
}

int sys_wait(int *status)
{
    while (1)
    {
        bool has_children = false;
        process_t *p = process_list;
        while (p)
        {
            if (p->parent == current_process)
            {
                has_children = true;
                if (p->terminated)
                {
                    if (status)
                        *status = p->exit_code;
                    int pid = p->pid;
                    p->parent = NULL; // Detach
                    return pid;
                }
            }
            p = p->next;
        }
        if (!has_children)
            return -1;
        schedule();
    }
}

int sys_getpid()
{
    return current_process->pid;
}

// Need to declare sys_read
int sys_read(int fd, char *buf, size_t count);

void sys_exec(const char *path, struct syscall_regs *regs)
{
    uint64_t entry_point;
    uint64_t cr3;
    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
    if (!elf_load(path, &entry_point, (pml4_t)cr3))
    {
        printf("sys_exec: Failed to load %s\n", path);
        return;
    }

    // Allocate new stack
    uint64_t stack_top = 0x7FFFFFFFF000;
    uint64_t stack_size = 4 * 4096;
    uint64_t stack_base = stack_top - stack_size;

    // uint64_t cr3; // Already declared
    // __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
    pml4_t pml4 = (pml4_t)cr3;

    for (uint64_t addr = stack_base; addr < stack_top; addr += 4096)
    {
        void *phys = pmm_alloc_page();
        if (!phys)
        {
            printf("sys_exec: OOM stack\n");
            return;
        }
        vmm_map_page(pml4, addr, (uint64_t)phys, PTE_PRESENT | PTE_WRITABLE | PTE_USER);
    }

    // Update user_rsp_scratch
    user_rsp_scratch = stack_top;

    // Update RIP (RCX in regs)
    regs->rcx = entry_point;

    // Reset RFLAGS (R11 in regs)
    regs->r11 = 0x202;
}

uint64_t syscall_handler(uint64_t syscall_number, uint64_t arg1, uint64_t arg2, uint64_t arg3, struct syscall_regs *regs)
{
    // Enable interrupts to allow I/O
    __asm__ volatile("sti");

    // printf("Syscall %lu\n", syscall_number);

#ifdef TEST_MODE
    test_syscall_count++;
    test_syscall_last_num = syscall_number;
    test_syscall_last_arg1 = arg1;
#endif

    switch (syscall_number)
    {
    case SYS_WRITE:
        sys_write((int)arg1, (const char *)arg2, (size_t)arg3);
        return 0;
    case SYS_EXIT:
        sys_exit((int)arg1);
        return 0;
    case SYS_EXEC:
        sys_exec((const char *)arg1, regs);
        return 0;
    case SYS_SPAWN:
        return sys_spawn((const char *)arg1);
    case SYS_WAIT:
        return sys_wait((int *)arg1);
    case SYS_GETPID:
        return sys_getpid();
    case SYS_YIELD:
        schedule();
        return 0;
    case SYS_READ:
        return sys_read((int)arg1, (char *)arg2, (size_t)arg3);
    default:
        printf("Unknown syscall: %lu\n", syscall_number);
        return -1;
    }
}

int sys_read(int fd, char *buf, size_t count)
{
    if (fd == 0)
    { // stdin
        size_t read = 0;
        while (read < count)
        {
            char c = keyboard_get_char();
            if (c)
            {
                buf[read++] = c;
            }
            else
            {
                if (read == 0)
                {
                    schedule();
                }
                else
                {
                    break;
                }
            }
        }
        return read;
    }
    return 0;
}
