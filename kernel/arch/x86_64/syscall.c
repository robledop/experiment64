#include <syscall.h>
#include "cpu.h"
#include "gdt.h"
#include "terminal.h"
#include "elf.h"
#include "vmm.h"
#include "pmm.h"
#include "process.h"
#include "keyboard.h"
#include "uart.h"
#include "string.h"
#include "heap.h"
#include <stdint.h>

extern void syscall_entry(void);
extern void fork_return(void);

#define TIMER_TICK_MS 10

struct syscall_regs
{
    uint64_t rdi, rsi, rdx, r10, r8, r9;
    uint64_t r15, r14, r13, r12, rbx, rbp;
    uint64_t rcx, r11;
};

#define SYSCALL_MAX_PATH VFS_MAX_PATH
#define SYSCALL_MAX_SEGMENTS 64

static void safe_copy(char *dst, size_t dst_size, const char *src)
{
    if (!dst || dst_size == 0)
        return;
    if (!src)
    {
        dst[0] = '\0';
        return;
    }

    size_t i = 0;
    while (i + 1 < dst_size && src[i])
    {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static void simplify_path(char *path)
{
    char buffer[SYSCALL_MAX_PATH];
    safe_copy(buffer, sizeof(buffer), path);

    char *segments[SYSCALL_MAX_SEGMENTS];
    int seg_count = 0;
    char *p = buffer;

    while (*p)
    {
        while (*p == '/')
            p++;
        if (!*p)
            break;

        char *segment = p;
        while (*p && *p != '/')
            p++;
        if (*p)
            *p++ = '\0';

        if (strcmp(segment, ".") == 0)
            continue;
        if (strcmp(segment, "..") == 0)
        {
            if (seg_count > 0)
                seg_count--;
            continue;
        }
        if (seg_count < SYSCALL_MAX_SEGMENTS)
            segments[seg_count++] = segment;
    }

    size_t idx = 0;
    path[idx++] = '/';
    for (int i = 0; i < seg_count && idx < SYSCALL_MAX_PATH - 1; i++)
    {
        const char *seg = segments[i];
        for (size_t j = 0; seg[j] && idx < SYSCALL_MAX_PATH - 1; j++)
            path[idx++] = seg[j];
        if (i != seg_count - 1 && idx < SYSCALL_MAX_PATH - 1)
            path[idx++] = '/';
    }

    if (idx == 1)
        path[1] = '\0';
    else
        path[idx] = '\0';
}

static void build_absolute_path(const char *base, const char *input, char *output, size_t size)
{
    const char *root = (base && base[0]) ? base : "/";

    if (!input || !*input)
    {
        safe_copy(output, size, root);
        return;
    }

    if (*input == '/')
    {
        safe_copy(output, size, input);
    }
    else
    {
        safe_copy(output, size, root);
        size_t idx = strlen(output);
        if (idx == 0)
        {
            output[0] = '/';
            output[1] = '\0';
            idx = 1;
        }
        if (idx > 1 && output[idx - 1] != '/' && idx + 1 < size)
            output[idx++] = '/';

        for (size_t i = 0; input[i] && idx + 1 < size; i++)
            output[idx++] = input[i];
        output[idx] = '\0';
    }

    simplify_path(output);
}

static int resolve_user_path(const char *path, char *resolved, size_t size)
{
    if (!resolved || size == 0)
        return -1;

    const char *base = (current_process && current_process->cwd[0]) ? current_process->cwd : "/";
    build_absolute_path(base, path, resolved, size);
    return 0;
}

// Forward declarations
int sys_open(const char *path);
int sys_close(int fd);
int sys_readdir(int fd, vfs_dirent_t *dent);
int64_t sys_sbrk(int64_t increment);
void sys_exit(int code);
int sys_wait(int *status);
int sys_getpid(void);
int sys_read(int fd, char *buf, size_t count);
void sys_write(int fd, const char *buf, size_t count);
void sys_exec(const char *path, struct syscall_regs *regs);
int sys_spawn(const char *path);
int sys_fork(struct syscall_regs *regs);
int sys_chdir(const char *path);
int sys_sleep(uint64_t milliseconds);

// Current kernel stack for syscalls (updated on context switch)
// Initialized to a temporary stack until scheduler takes over
static uint8_t bootstrap_stack[4096];

void syscall_set_stack(uint64_t stack_top)
{
    cpu_t *cpu = get_cpu();
    cpu->kernel_rsp = stack_top;
    tss_set_stack(stack_top);
}

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
    // For BSP, we use bootstrap stack initially
    // For APs, we should probably have a stack allocated or use what's set
    cpu_t *cpu = get_cpu();
    if (cpu->kernel_rsp == 0)
    {
        // Only set if not already set (e.g. by scheduler)
        // For BSP this is fine. For APs, they should have a stack.
        // But APs don't have a bootstrap stack variable per CPU.
        // However, APs are started with a stack by Limine?
        // Limine gives APs a stack in `goto_address`.
        // But we need a kernel stack for syscalls.
        // If we don't have one, we can't handle syscalls yet.
        // But syscall_init is called early.
        // For BSP, use bootstrap_stack.
        if (cpu->lapic_id == 0)
        { // Assuming BSP is 0, which might not be true but usually is.
          // Better: check if it's BSP.
          // But we don't know if we are BSP easily here without looking up.
          // Let's just use bootstrap_stack for everyone? NO, race condition.
          // For now, let's assume scheduler sets it up before running user code.
          // But we need it for `syscall_set_stack` to work?
          // `syscall_set_stack` is called by scheduler.
          // So we just need to init the MSRs here.
          // But we also call `tss_set_stack` here.
          // For BSP, we can init it.
          // For APs, maybe skip stack init here and let scheduler do it?
          // But `tss_set_stack` is needed for interrupts too.
          // APs need a valid TSS RSP0 for interrupts.
          // Where do APs get their stack?
          // In `ap_main`, they run on a stack provided by Limine (or us).
          // We should set TSS RSP0 to current stack?
          // Or allocate one.
        }
    }

    // For BSP, init with bootstrap stack
    static bool bsp_initialized = false;
    if (!bsp_initialized)
    {
        syscall_set_stack((uint64_t)bootstrap_stack + 4096);
        bsp_initialized = true;
    }
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

    uint64_t stack = current_thread->user_stack;
    uint64_t entry = current_thread->user_entry;

    __asm__ volatile(
        "mov %0, %%ds\n"
        "mov %0, %%es\n"
        "mov %0, %%fs\n"
        "mov %0, %%gs\n"
        "pushq %0\n"         // SS
        "pushq %1\n"         // RSP
        "pushq %2\n"         // RFLAGS
        "pushq %3\n"         // CS
        "pushq %4\n"         // RIP
        "xor %%rdi, %%rdi\n" // argc = 0
        "xor %%rsi, %%rsi\n" // argv = NULL
        "iretq\n"
        :
        : "r"(user_ss), "r"(stack), "r"(rflags), "r"(user_cs), "r"(entry)
        : "memory", "rdi", "rsi");
}

int sys_spawn(const char *path)
{
    if (!path || !*path)
        return -1;
    char abs_path[SYSCALL_MAX_PATH];
    if (resolve_user_path(path, abs_path, sizeof(abs_path)) != 0)
        return -1;

    pml4_t new_pml4 = vmm_new_pml4();
    if (!new_pml4)
        return -1;

    uint64_t entry_point;
    uint64_t max_vaddr;
    if (!elf_load(abs_path, &entry_point, &max_vaddr, new_pml4))
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
    proc->heap_end = max_vaddr;

    thread_t *thread = thread_create(proc, spawn_trampoline, false);
    thread->user_entry = entry_point;
    thread->user_stack = stack_top;

    return proc->pid;
}

int sys_fork(struct syscall_regs *regs)
{
    // 1. Copy Address Space
    pml4_t child_pml4 = vmm_copy_pml4(current_process->pml4);
    if (!child_pml4)
        return -1;

    // 2. Create Process
    process_t *child_proc = process_create(current_process->name);
    if (!child_proc)
        return -1;

    child_proc->pml4 = child_pml4;
    child_proc->parent = current_process;

    // 3. Create Thread
    thread_t *child_thread = thread_create(child_proc, NULL, true);
    if (!child_thread)
        return -1;

    // 4. Setup Child Stack
    // We need to place syscall_regs and a new context on the stack.
    // thread_create already put a context at the top, but we need to push regs first.
    // Stack Layout (High to Low):
    // [ ... ] <- kstack_top
    // [ Syscall Regs ]
    // [ Context ] <- thread->context

    uint64_t stack_top = child_thread->kstack_top;
    uint64_t regs_size = sizeof(struct syscall_regs);
    uint64_t context_size = sizeof(struct context);

    // Pointer to where regs should be
    struct syscall_regs *child_regs = (struct syscall_regs *)(stack_top - regs_size);
    *child_regs = *regs; // Copy user registers

    // Pointer to where context should be
    struct context *child_ctx = (struct context *)((uint64_t)child_regs - context_size);
    memset(child_ctx, 0, context_size);

    child_ctx->rip = (uint64_t)fork_return;
    // Other registers in context can be 0, they are kernel registers restored by switch_to.
    // The stack pointer (rsp) will be set to child_ctx by switch_to logic (it loads rsp from thread->context).
    // Wait, switch_to does: mov [prev->context], rsp; mov rsp, [next->context]; pop ... ret
    // So when we switch to child, rsp becomes child_ctx.
    // Then it pops r15..rbx, rbp, r12, r13, r14.
    // Then ret.
    // The ret will pop rip (which is fork_return).
    // At that point rsp will be child_ctx + sizeof(context) == child_regs.
    // fork_return expects rsp to point to child_regs.
    // Perfect.

    child_thread->context = child_ctx;
    cpu_t *cpu = get_cpu();
    child_thread->saved_user_rsp = cpu->user_rsp; // Inherit user stack pointer

    return child_proc->pid;
}

int sys_chdir(const char *path)
{
    if (!path || !*path)
        return -1;
    char abs_path[SYSCALL_MAX_PATH];
    resolve_user_path(path, abs_path, sizeof(abs_path));

    vfs_inode_t *node = vfs_resolve_path(abs_path);
    if (!node)
        return -1;
    if ((node->flags & 0x07) != VFS_DIRECTORY)
        return -1;

    safe_copy(current_process->cwd, sizeof(current_process->cwd), abs_path);
    return 0;
}

int sys_sleep(uint64_t milliseconds)
{
    if (milliseconds == 0)
    {
        schedule();
        return 0;
    }

    uint64_t ticks = (milliseconds + TIMER_TICK_MS - 1) / TIMER_TICK_MS;
    if (ticks == 0)
        ticks = 1;

    uint64_t target = scheduler_ticks + ticks;
    current_thread->sleep_until = target;
    current_thread->state = THREAD_BLOCKED;
    schedule();
    return 0;
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
    if (!path || !*path)
        return;
    char abs_path[SYSCALL_MAX_PATH];
    resolve_user_path(path, abs_path, sizeof(abs_path));

    uint64_t entry_point;
    uint64_t max_vaddr = 0;
    uint64_t cr3;
    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
    if (!elf_load(abs_path, &entry_point, &max_vaddr, (pml4_t)cr3))
    {
        printf("sys_exec: Failed to load %s\n", path);
        return;
    }
    current_process->heap_end = max_vaddr;

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
    cpu_t *cpu = get_cpu();
    cpu->user_rsp = stack_top;

    // Update RIP (RCX in regs)
    regs->rcx = entry_point;

    // Reset RFLAGS (R11 in regs)
    regs->r11 = 0x202;

    // Clear registers
    regs->rdi = 0;
    regs->rsi = 0;
    regs->rdx = 0;
    regs->r10 = 0;
    regs->r8 = 0;
    regs->r9 = 0;
    // regs->rax is not in struct syscall_regs, but return value of handler sets it.
    regs->rbx = 0;
    regs->rbp = 0;
    regs->r12 = 0;
    regs->r13 = 0;
    regs->r14 = 0;
    regs->r15 = 0;
}

int64_t sys_sbrk(int64_t increment)
{
    uint64_t old_brk = current_process->heap_end;
    uint64_t new_brk = old_brk + increment;

    // Align to page size for mapping
    uint64_t old_page_end = (old_brk + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    uint64_t new_page_end = (new_brk + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);

    if (increment > 0)
    {
        for (uint64_t addr = old_page_end; addr < new_page_end; addr += PAGE_SIZE)
        {
            void *phys = pmm_alloc_page();
            if (!phys)
            {
                return -1; // OOM
            }
            vmm_map_page(current_process->pml4, addr, (uint64_t)phys, PTE_PRESENT | PTE_WRITABLE | PTE_USER);
            memset((void *)addr, 0, PAGE_SIZE); // Zero out new memory
        }
    }
    else if (increment < 0)
    {
        // Shrinking heap
        // We could unmap pages here if we wanted to be thorough
    }

    current_process->heap_end = new_brk;
    return (int64_t)old_brk;
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
    case SYS_FORK:
        return sys_fork(regs);
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
    case SYS_SBRK:
        return sys_sbrk((int64_t)arg1);
    case SYS_OPEN:
        return sys_open((const char *)arg1);
    case SYS_CLOSE:
        return sys_close((int)arg1);
    case SYS_READDIR:
        return sys_readdir((int)arg1, (vfs_dirent_t *)arg2);
    case SYS_CHDIR:
        return sys_chdir((const char *)arg1);
    case SYS_SLEEP:
        return sys_sleep(arg1);
    default:
        printf("Unknown syscall: %lu\n", syscall_number);
        return -1;
    }
}

int sys_read(int fd, char *buf, size_t count)
{
    printf("SYS_READ: fd=%d count=%lu\n", fd, count);
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

    if (fd >= 3 && fd < MAX_FDS)
    {
        file_descriptor_t *desc = current_process->fd_table[fd];
        if (desc && desc->inode)
        {
            uint64_t read = vfs_read(desc->inode, desc->offset, count, (uint8_t *)buf);
            desc->offset += read;
            return read;
        }
    }
    return 0;
}

int sys_open(const char *path)
{
    if (!path || !*path)
        return -1;
    char abs_path[SYSCALL_MAX_PATH];
    resolve_user_path(path, abs_path, sizeof(abs_path));
    int fd = -1;
    for (int i = 3; i < MAX_FDS; i++)
    {
        if (current_process->fd_table[i] == NULL)
        {
            fd = i;
            break;
        }
    }
    if (fd == -1)
        return -1;

    vfs_inode_t *inode = vfs_resolve_path(abs_path);
    if (!inode)
        return -1;

    file_descriptor_t *desc = kmalloc(sizeof(file_descriptor_t));
    if (!desc)
        return -1;

    desc->inode = inode;
    desc->offset = 0;
    current_process->fd_table[fd] = desc;

    vfs_open(inode);
    return fd;
}

int sys_close(int fd)
{
    if (fd < 3 || fd >= MAX_FDS)
        return -1;
    file_descriptor_t *desc = current_process->fd_table[fd];
    if (!desc)
        return -1;

    vfs_close(desc->inode);
    // We should probably free the inode if it was allocated by resolve_path/finddir?
    // vfs_finddir allocates a new inode struct in fat32.
    // Yes, fat32_vfs_finddir does kmalloc.
    // So we should free desc->inode too.
    if (desc->inode != vfs_root)
    {
        kfree(desc->inode);
    }
    kfree(desc);
    current_process->fd_table[fd] = NULL;
    return 0;
}

int sys_readdir(int fd, vfs_dirent_t *dent)
{
    if (fd < 3 || fd >= MAX_FDS)
        return -1;
    file_descriptor_t *desc = current_process->fd_table[fd];
    if (!desc)
        return -1;

    vfs_dirent_t *d = vfs_readdir(desc->inode, desc->offset);
    if (!d)
        return 0; // End of directory

    memcpy(dent, d, sizeof(vfs_dirent_t));
    kfree(d);
    desc->offset++;
    return 1; // Success
}
