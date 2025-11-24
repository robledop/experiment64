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
#include <limits.h>

static int clamp_size_to_int(size_t value)
{
    if (value > (size_t)INT_MAX)
        return INT_MAX;
    return (int)value;
}

static int clamp_u64_to_int(uint64_t value)
{
    if (value > (uint64_t)INT_MAX)
        return INT_MAX;
    return (int)value;
}
#include "string.h"
#include "heap.h"
#include <stdint.h>

#ifdef TEST_MODE
volatile uint64_t test_syscall_count = 0;
volatile uint64_t test_syscall_last_num = 0;
volatile uint64_t test_syscall_last_arg1 = 0;
#endif

uint8_t bootstrap_stack[4096];

void syscall_set_stack(uint64_t stack)
{
    cpu_t *cpu = get_cpu();
    cpu->kernel_rsp = stack;
    tss_set_stack(stack);
}

extern void syscall_entry(void);
extern void fork_return(void);
extern void fork_child_trampoline(void);

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
int sys_write(int fd, const char *buf, size_t count);
int sys_exec(const char *path, struct syscall_regs *regs);
int sys_spawn(const char *path);
int sys_fork(struct syscall_regs *regs);
int sys_chdir(const char *path);
int sys_sleep(uint64_t milliseconds);
int sys_mknod(const char *path, int mode, int dev);

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
    wrmsr(MSR_SFMASK, RFLAGS_IF);

    // Set TSS RSP0 to the kernel stack
    // For BSP, we use bootstrap stack initially
    // For APs, we should probably have a stack allocated or use what's set
    cpu_t *cpu = get_cpu();
    if (cpu->kernel_rsp == 0)
    {
        // For BSP, use bootstrap_stack if no stack is set.
        if (cpu->lapic_id == 0)
        {
            cpu->kernel_rsp = (uint64_t)bootstrap_stack + sizeof(bootstrap_stack);
            tss_set_stack(cpu->kernel_rsp);
        }
    }
}

static void (*exit_hook)(int) = NULL;

void syscall_set_exit_hook(void (*hook)(int))
{
    exit_hook = hook;
}

int sys_write(int fd, const char *buf, size_t count)
{
    if (fd == 1 || fd == 2) // stdout or stderr
    {
        terminal_write(buf, count);
        return clamp_size_to_int(count);
    }

    if (fd < 0 || fd >= MAX_FDS || !current_process->fd_table[fd])
        return -1;

    file_descriptor_t *desc = current_process->fd_table[fd];
    uint64_t written = vfs_write(desc->inode, desc->offset, count, (uint8_t *)buf);
    desc->offset += written;
    return clamp_u64_to_int(written);
}

void sys_exit(int code)
{
    if (exit_hook)
    {
        exit_hook(code);
    }
    printk("Process %d exited with code %d\n", current_process->pid, code);
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
        "cli\n"
        "swapgs\n"
        "mov ds, %0\n"
        "mov es, %0\n"
        "mov fs, %0\n"
        "mov gs, %0\n"
        "push %0\n"      // SS
        "push %1\n"      // RSP
        "push %2\n"      // RFLAGS
        "push %3\n"      // CS
        "push %4\n"      // RIP
        "xor rdi, rdi\n" // argc = 0
        "xor rsi, rsi\n" // argv = NULL
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

    process_copy_fds(proc, current_process);

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

    process_copy_fds(child_proc, current_process);

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

    child_ctx->rip = (uint64_t)fork_child_trampoline;
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

int sys_getpid(void)
{
    return current_process->pid;
}

int sys_wait(int *status)
{
    while (1)
    {
        bool has_children = false;
        process_t *p, *next_p;
        list_for_each_entry_safe(p, next_p, &process_list, list)
        {
            if (p->parent == current_process)
            {
                has_children = true;
                if (p->terminated)
                {
                    if (status)
                    {
                        if ((uint64_t)status < 0x800000000000)
                            *status = p->exit_code;
                    }
                    int pid = p->pid;

                    process_destroy(p);

                    return pid;
                }
            }
        }
        if (!has_children)
        {
            return -1;
        }
        schedule();
    }
}

int sys_exec(const char *path, struct syscall_regs *regs)
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
        // vmm_destroy_pml4(new_pml4); // Not implemented fully?
        return -1;
    }

    // Switch to new PML4
    // pml4_t old_pml4 = current_process->pml4;
    current_process->pml4 = new_pml4;
    vmm_switch_pml4(new_pml4);
    // vmm_destroy_pml4(old_pml4);

    uint64_t stack_top = 0x7FFFFFFFF000;
    uint64_t stack_size = 4 * 4096;
    uint64_t stack_base = stack_top - stack_size;

    for (uint64_t addr = stack_base; addr < stack_top; addr += 4096)
    {
        void *phys = pmm_alloc_page();
        vmm_map_page(new_pml4, addr, (uint64_t)phys, PTE_PRESENT | PTE_WRITABLE | PTE_USER);
    }

    current_process->heap_end = max_vaddr;
    regs->rcx = entry_point;
    get_cpu()->user_rsp = stack_top;

    return 0;
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
    {
        if (node != vfs_root)
        {
            vfs_close(node);
            kfree(node);
        }
        return -1;
    }

    safe_copy(current_process->cwd, sizeof(current_process->cwd), abs_path);
    if (node != vfs_root)
    {
        vfs_close(node);
        kfree(node);
    }
    return 0;
}

int sys_sleep(uint64_t milliseconds)
{
    uint64_t start = scheduler_ticks;
    uint64_t ticks = milliseconds / TIMER_TICK_MS;
    if (ticks == 0)
        ticks = 1;

    while (scheduler_ticks < start + ticks)
    {
        schedule();
    }
    return 0;
}

int sys_mknod(const char *path, int mode, int dev)
{
    if ((uint64_t)path >= 0x800000000000) // Check if user pointer
        return -1;

    char kpath[VFS_MAX_PATH];
    safe_copy(kpath, sizeof(kpath), path);
    simplify_path(kpath);

    return vfs_mknod(kpath, mode, dev);
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

#ifdef TEST_MODE
    test_syscall_count++;
    test_syscall_last_num = syscall_number;
    test_syscall_last_arg1 = arg1;
#endif

    switch (syscall_number)
    {
    case SYS_WRITE:
        return sys_write((int)arg1, (const char *)arg2, (size_t)arg3);
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
    case SYS_MKNOD:
        return sys_mknod((const char *)arg1, (int)arg2, (int)arg3);
    default:
        printk("Unknown syscall: %lu\n", syscall_number);
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
            if (read > 0 && !keyboard_has_char())
            {
                break;
            }

            char c = keyboard_get_char();
            if (c)
            {
                buf[read++] = c;
            }
        }
        return clamp_size_to_int(read);
    }

    if (fd >= 3 && fd < MAX_FDS)
    {
        file_descriptor_t *desc = current_process->fd_table[fd];
        if (desc && desc->inode)
        {
            uint64_t read = vfs_read(desc->inode, desc->offset, count, (uint8_t *)buf);
            desc->offset += read;
            return clamp_u64_to_int(read);
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

    if (desc->inode != vfs_root)
    {
        vfs_close(desc->inode);
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
