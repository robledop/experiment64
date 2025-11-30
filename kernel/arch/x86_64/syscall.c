#include <syscall.h>
#include "cpu.h"
#include "gdt.h"
#include "terminal.h"
#include "elf.h"
#include "vmm.h"
#include "pmm.h"
#include "process.h"
#include "keyboard.h"
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include "ioctl.h"
#include "string.h"
#include "heap.h"
#include "framebuffer.h"
#include "apic.h"
#include "mman.h"
#include "util.h"
#include "fcntl.h"
#include "io.h"
#include "vfs.h"
#include "time.h"
#include "tsc.h"
#include "path.h"
#include "pipe.h"

#ifdef KASAN
#include "kasan.h"
#endif

int sys_close(int fd);
int sys_readdir(int fd, vfs_dirent_t *dent);
int64_t sys_sbrk(int64_t increment);
void sys_exit(int code);
int sys_wait(int *status);
int sys_getpid(void);
int sys_read(int fd, char *buf, size_t count);
int sys_write(int fd, const char *buf, size_t count);
int sys_exec(const char *path, struct syscall_regs *regs);
int sys_execve(const char *path, const char *const argv[], const char *const envp[], struct syscall_regs *regs);
int sys_spawn(const char *path);
int sys_fork(struct syscall_regs *regs);
int sys_chdir(const char *path);
int sys_sleep(uint64_t milliseconds);
int sys_usleep(uint64_t usec);
int sys_mknod(const char *path, int mode, int dev);
int sys_ioctl(int fd, int request, void *arg);
int sys_open(const char *path, int flags);
void *sys_mmap(void *addr, size_t length, int prot, int flags, int fd, size_t offset);
int sys_munmap(void *addr, size_t length);
int sys_stat(const char *path, struct stat *st);
int sys_fstat(int fd, struct stat *st);
int sys_link(const char *oldpath, const char *newpath);
int sys_unlink(const char *path);
int sys_gettimeofday(struct timeval *tv, struct timezone *tz);
int sys_pipe(int pipefd[2]);
long sys_lseek(int fd, long offset, int whence);
int sys_dup(int oldfd);
int sys_kill(int pid, int sig);
void sys_shutdown();
void sys_reboot();

// ReSharper disable once CppDFAConstantFunctionResult
static bool prepare_user_buffer(void *addr, const size_t size, const bool is_write)
{
    (void)is_write;
    if (!addr || size == 0)
        return true;
#ifdef KASAN
    if (kasan_is_ready())
    {
        if (is_write)
        {
            kasan_unpoison_range(addr, size);
        }
        else if (!kasan_check_range(addr, size, false, __builtin_return_address(0)))
        {
            return false;
        }
    }
#endif
    return true;
}

static bool copy_to_user(void *dst, const void *src, size_t size)
{
    if (!dst || !src)
        return false;
    // ReSharper disable once CppDFAConstantConditions
    if (!prepare_user_buffer(dst, size, true))
        return false;
    memcpy(dst, src, size);
    return true;
}

static bool __attribute__((unused)) copy_from_user(void *dst, const void *src, size_t size)
{
    if (!dst || !src)
        return false;
    // ReSharper disable once CppDFAConstantConditions
    if (!prepare_user_buffer((void *)src, size, false))
        return false;
    memcpy(dst, src, size);
    return true;
}

static bool fd_can_read(const file_descriptor_t *desc)
{
    if (!desc)
        return false;
    const int mode = desc->flags & (O_WRONLY | O_RDWR);
    return mode != O_WRONLY;
}

static bool fd_can_write(const file_descriptor_t *desc)
{
    if (!desc)
        return false;
    const int mode = desc->flags & (O_WRONLY | O_RDWR);
    return mode == O_WRONLY || mode == O_RDWR || mode == (O_WRONLY | O_RDWR);
}

static void fill_stat_from_inode(const vfs_inode_t *inode, struct stat *st)
{
    if (!inode || !st)
        return;
    st->dev = 0;
    st->ino = (int)inode->inode;
    st->type = (int)(inode->flags & 0x07);
    st->nlink = 1;
    st->size = inode->size;
    st->ref = 0;
    st->i_atime = 0;
    st->i_ctime = 0;
    st->i_mtime = 0;
    st->i_dtime = 0;
    st->i_uid = 0;
    st->i_gid = 0;
    st->i_flags = 0;
}

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

static void set_process_name_from_path(process_t *proc, const char *path)
{
    if (!proc || !path)
        return;
    const char *name = path;
    for (const char *p = path; *p; p++)
    {
        if (*p == '/' && p[1])
            name = p + 1;
    }
    path_safe_copy(proc->name, sizeof(proc->name), name);
}

#define EXEC_MAX_ARGS 16
#define EXEC_MAX_ARG_LEN 128

static int copy_in_args(const char *const *argv, char args[EXEC_MAX_ARGS][EXEC_MAX_ARG_LEN])
{
    if (!argv)
        return 0;

    int count = 0;
    while (count < EXEC_MAX_ARGS)
    {
        const char *user_arg = argv[count];
        if (!user_arg)
            break;

        if (!prepare_user_buffer((void *)user_arg, 1, false))
            return -1;

        size_t len = 0;
        while (len + 1 < EXEC_MAX_ARG_LEN && user_arg[len])
            len++;
        if (len + 1 >= EXEC_MAX_ARG_LEN && user_arg[len])
            return -1; // argument too long

        memcpy(args[count], user_arg, len);
        args[count][len] = '\0';
        count++;
    }
    return count;
}

static int setup_user_stack(const uint64_t *pml4, uint64_t stack_top,
                            const char args[EXEC_MAX_ARGS][EXEC_MAX_ARG_LEN], int argc, uint64_t *out_rsp)
{
    uint64_t sp = stack_top;
    uint64_t arg_ptrs[EXEC_MAX_ARGS];

    for (int i = argc - 1; i >= 0; i--)
    {
        size_t len = strlen(args[i]) + 1;
        sp -= len;
        memcpy((void *)sp, args[i], len);
        arg_ptrs[i] = sp;
    }

    // Align stack to 16 bytes
    sp &= ~0xFul;

    // argv terminator
    sp -= sizeof(uint64_t);
    *(uint64_t *)sp = 0;

    // argv pointers
    for (int i = argc - 1; i >= 0; i--)
    {
        sp -= sizeof(uint64_t);
        *(uint64_t *)sp = arg_ptrs[i];
    }

    // argc
    sp -= sizeof(uint64_t);
    *(uint64_t *)sp = (uint64_t)argc;

    *out_rsp = sp;
    (void)pml4;
    return 0;
}

static int resolve_user_path(const char *path, char *resolved, size_t size)
{
    if (!resolved || size == 0)
        return -1;

    const char *base = (current_process && current_process->cwd[0]) ? current_process->cwd : "/";
    path_build_absolute(base, path, resolved, size);
    return 0;
}

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

static void (*exit_hook)(int) = nullptr;

void syscall_set_exit_hook(void (*hook)(int))
{
    exit_hook = hook;
}

int sys_write(int fd, const char *buf, size_t count)
{
    if (fd < 0 || fd >= MAX_FDS)
        return -1;

    if (!prepare_user_buffer((void *)buf, count, false))
        return -1;

    file_descriptor_t *desc = current_process->fd_table[fd];

    // Handle stdout/stderr (fd 1/2) specially - check if redirected to pipe/file
    if (fd == 1 || fd == 2)
    {
        // If fd has a real descriptor with an inode, write to it (pipe or file redirection)
        if (desc && desc->inode)
        {
            if (!fd_can_write(desc))
                return -1;
            if (desc->flags & O_APPEND)
                desc->offset = desc->inode->size;
            uint64_t written = vfs_write(desc->inode, desc->offset, count, (uint8_t *)buf);
            desc->offset += written;
            return clamp_to_int(written);
        }

        // No inode - write to terminal (console device)
        if (desc && !fd_can_write(desc))
            return -1;

        terminal_write(buf, count);
        return clamp_to_int(count);
    }

    if (!desc || !desc->inode || !fd_can_write(desc))
        return -1;

    if (desc->flags & O_APPEND)
        desc->offset = desc->inode->size;

    uint64_t written = vfs_write(desc->inode, desc->offset, count, (uint8_t *)buf);
    desc->offset += written;
    return clamp_to_int(written);
}

void sys_exit(int code)
{
    if (exit_hook)
    {
        exit_hook(code);
    }
    // printk("Process %d exited with code %d\n", current_process->pid, code);
    current_process->exit_code = code;
    current_process->terminated = true;
    current_thread->state = THREAD_TERMINATED;
    if (current_process->parent)
        thread_wakeup(current_process->parent);

    // Save current thread state
    thread_t *current = get_current_thread();
    if (current)
    {
        // Save context if needed (already saved by interrupt handler)
    }

    schedule();
}

int sys_kill(int pid, int sig)
{
    (void)sig; // For now, any signal terminates the process

    // Find the target process
    process_t *target = nullptr;
    list_head_t *pos;
    list_for_each(pos, &process_list)
    {
        process_t *p = list_entry(pos, process_t, list);
        if (p->pid == pid)
        {
            target = p;
            break;
        }
    }

    if (!target)
        return -1; // Process not found

    // Don't allow killing the kernel process or init
    if (target->pid <= 1)
        return -1;

    // Mark the process as terminated
    target->exit_code = 128 + sig; // Convention: exit code = 128 + signal number
    target->terminated = true;

    // Terminate all threads of the process
    list_head_t *thread_pos;
    list_for_each(thread_pos, &target->threads)
    {
        thread_t *t = list_entry(thread_pos, thread_t, list);
        t->state = THREAD_TERMINATED;
    }

    // Wake up the parent if it's waiting
    if (target->parent)
        thread_wakeup(target->parent);

    // If we killed ourselves, reschedule
    if (target == current_process)
        schedule();

    return 0;
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
        "xor rsi, rsi\n" // argv = nullptr
        "iretq\n"
        :
        : "r"(user_ss), "r"(stack), "r"(rflags), "r"(user_cs), "r"(entry)
        : "memory", "rdi", "rsi");
}

int sys_spawn(const char *path)
{
    if (!path || !*path)
        return -1;
    char abs_path[VFS_MAX_PATH];
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
    set_process_name_from_path(proc, abs_path);
    proc->pml4 = new_pml4;
    proc->parent = current_process;
    proc->heap_end = max_vaddr;

    process_copy_fds(proc, current_process);
    vm_area_add(proc, stack_base, stack_top, VMA_READ | VMA_WRITE | VMA_USER | VMA_STACK);

    thread_t *thread = thread_create(proc, spawn_trampoline, false);
    thread->user_entry = entry_point;
    thread->user_stack = stack_top;

    return proc->pid;
}

int sys_fork(struct syscall_regs *regs)
{
    if (!regs)
        return -1;

    // Copy Address Space
    pml4_t child_pml4 = vmm_copy_pml4(current_process->pml4);
    if (!child_pml4)
        return -1;

    // Create Process
    process_t *child_proc = process_create(current_process->name);
    if (!child_proc)
        return -1;

    child_proc->pml4 = child_pml4;
    child_proc->parent = current_process;
    child_proc->heap_end = current_process->heap_end;

    process_copy_fds(child_proc, current_process);
    vm_area_clone(child_proc, current_process);

    // Create Thread
    thread_t *child_thread = thread_create(child_proc, nullptr, true);
    if (!child_thread)
        return -1;

    // Setup Child Stack
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
                    if (status && (uint64_t)status < 0x800000000000)
                    {
                        int code = p->exit_code;
                        copy_to_user(status, &code, sizeof(int));
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
        thread_sleep(current_process, nullptr);
    }
}

int sys_exec(const char *path, struct syscall_regs *regs)
{
    // Add a null terminator so copy_in_args stops after the single path entry.
    const char *argv[2] = {path, nullptr};
    return sys_execve(path, argv, nullptr, regs);
}

int sys_execve(const char *path, const char *const argv[], [[maybe_unused]] const char *const envp[],
               struct syscall_regs *regs)
{
    if (!path || !*path)
        return -1;

    char abs_path[VFS_MAX_PATH];
    if (resolve_user_path(path, abs_path, sizeof(abs_path)) != 0)
        return -1;

    char args[EXEC_MAX_ARGS][EXEC_MAX_ARG_LEN];
    int argc = copy_in_args(argv, args);
    if (argc < 0)
        return -1;
    if (argc == 0)
    {
        path_safe_copy(args[0], EXEC_MAX_ARG_LEN, abs_path);
        argc = 1;
    }

    pml4_t new_pml4 = vmm_new_pml4();
    if (!new_pml4)
        return -1;

    uint64_t entry_point;
    uint64_t max_vaddr;
    if (!elf_load(abs_path, &entry_point, &max_vaddr, new_pml4))
    {
        return -1;
    }

    current_process->pml4 = new_pml4;
    vmm_switch_pml4(new_pml4);

    uint64_t stack_top = 0x7FFFFFFFF000;
    uint64_t stack_size = 4 * 4096;
    uint64_t stack_base = stack_top - stack_size;

    for (uint64_t addr = stack_base; addr < stack_top; addr += 4096)
    {
        void *phys = pmm_alloc_page();
        vmm_map_page(new_pml4, addr, (uint64_t)phys, PTE_PRESENT | PTE_WRITABLE | PTE_USER);
    }

    uint64_t user_rsp = stack_top;
    if (setup_user_stack(new_pml4, stack_top, args, argc, &user_rsp) != 0)
        return -1;

    current_process->heap_end = max_vaddr;
    set_process_name_from_path(current_process, abs_path);
    regs->rcx = entry_point;
    get_cpu()->user_rsp = user_rsp;

    return 0;
}

int sys_chdir(const char *path)
{
    if (!path || !*path)
        return -1;
    char abs_path[VFS_MAX_PATH];
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

    path_safe_copy(current_process->cwd, sizeof(current_process->cwd), abs_path);
    if (node != vfs_root)
    {
        vfs_close(node);
        kfree(node);
    }
    return 0;
}

int sys_getcwd(char *buf, size_t size)
{
    if (!buf || size == 0)
        return -1;
    const char *cwd = (current_process && current_process->cwd[0]) ? current_process->cwd : "/";
    const size_t len = strlen(cwd);
    if (len + 1 > size)
        return -1;
    if (!prepare_user_buffer(buf, len + 1, true))
        return -1;
    memcpy(buf, cwd, len + 1);
    return 0;
}

int sys_gettimeofday(struct timeval *tv, struct timezone *tz)
{
    if (tv && !prepare_user_buffer(tv, sizeof(*tv), true))
        return -1;
    if (tz && !prepare_user_buffer(tz, sizeof(*tz), true))
        return -1;

    uint64_t ns = tsc_nanos();
    if (ns == 0)
        ns = scheduler_ticks * (1000000000ull / TIMER_FREQUENCY_HZ);

    if (tv)
    {
        tv->tv_sec = (int64_t)(ns / 1000000000ull);
        tv->tv_usec = (int64_t)((ns % 1000000000ull) / 1000ull);
    }
    if (tz)
    {
        tz->tz_minuteswest = 0;
        tz->tz_dsttime = 0;
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

int sys_usleep(uint64_t usec)
{
    if (usec == 0)
        return 0;

    const uint64_t tick_us = 1000000ull / TIMER_FREQUENCY_HZ;
    if (usec >= tick_us)
    {
        uint64_t ms = usec / 1000;
        if (usec % 1000)
            ms++;
        return sys_sleep(ms);
    }

    const uint64_t ns = usec * 1000;
    tsc_sleep_ns(ns);
    return 0;
}

int sys_mknod(const char *path, int mode, int dev)
{
    if ((uint64_t)path >= 0x800000000000) // Check if user pointer
        return -1;

    char kpath[VFS_MAX_PATH];
    path_safe_copy(kpath, sizeof(kpath), path);
    path_simplify(kpath, sizeof(kpath));

    return vfs_mknod(kpath, mode, dev);
}

int sys_stat(const char *path, struct stat *st)
{
    if (!path || !st)
        return -1;
    if (!prepare_user_buffer(st, sizeof(struct stat), true))
        return -1;

    char abs_path[VFS_MAX_PATH];
    resolve_user_path(path, abs_path, sizeof(abs_path));

    vfs_inode_t *inode = vfs_resolve_path(abs_path);
    if (!inode)
        return -1;

    fill_stat_from_inode(inode, st);
    if (inode != vfs_root)
    {
        vfs_close(inode);
        kfree(inode);
    }
    return 0;
}

int sys_link(const char *oldpath, const char *newpath)
{
    if (!oldpath || !newpath || !*oldpath || !*newpath)
        return -1;

    char abs_old[VFS_MAX_PATH];
    char abs_new[VFS_MAX_PATH];
    resolve_user_path(oldpath, abs_old, sizeof(abs_old));
    resolve_user_path(newpath, abs_new, sizeof(abs_new));

    return vfs_link(abs_old, abs_new);
}

int sys_unlink(const char *path)
{
    if (!path || !*path)
        return -1;

    char abs_path[VFS_MAX_PATH];
    resolve_user_path(path, abs_path, sizeof(abs_path));

    // Prevent unlinking the root
    if (strcmp(abs_path, "/") == 0)
        return -1;

    return vfs_unlink(abs_path);
}

int sys_fstat(int fd, struct stat *st)
{
    if (!st || fd < 0 || fd >= MAX_FDS)
        return -1;
    if (!prepare_user_buffer(st, sizeof(struct stat), true))
        return -1;

    file_descriptor_t *desc = current_process->fd_table[fd];
    if (!desc || !desc->inode)
        return -1;

    fill_stat_from_inode(desc->inode, st);
    return 0;
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
            memset((void *)addr, 0, PAGE_SIZE);
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

uint64_t syscall_handler(uint64_t syscall_number, uint64_t arg1, uint64_t arg2, uint64_t arg3,
                         struct syscall_regs *regs)
{
    // Enable interrupts to allow I/O
    __asm__ volatile("sti");

#ifdef TEST_MODE
    test_syscall_count++;
    test_syscall_last_num = syscall_number;
    test_syscall_last_arg1 = arg1;
#endif

    uint64_t arg4 = regs ? regs->r10 : 0;
    uint64_t arg5 = regs ? regs->r8 : 0;
    uint64_t arg6 = regs ? regs->r9 : 0;

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
    case SYS_EXECVE:
        return sys_execve((const char *)arg1, (const char *const *)arg2, (const char *const *)arg3, regs);
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
        return sys_open((const char *)arg1, (int)arg2);
    case SYS_CLOSE:
        return sys_close((int)arg1);
    case SYS_READDIR:
        return sys_readdir((int)arg1, (vfs_dirent_t *)arg2);
    case SYS_CHDIR:
        return sys_chdir((const char *)arg1);
    case SYS_SLEEP:
        return sys_sleep(arg1);
    case SYS_USLEEP:
        return sys_usleep(arg1);
    case SYS_MKNOD:
        return sys_mknod((const char *)arg1, (int)arg2, (int)arg3);
    case SYS_IOCTL:
        return sys_ioctl((int)arg1, (int)arg2, (void *)arg3);
    case SYS_MMAP:
        return (uint64_t)sys_mmap((void *)arg1, (size_t)arg2, (int)arg3, (int)arg4, (int)arg5, (size_t)arg6);
    case SYS_MUNMAP:
        return (uint64_t)sys_munmap((void *)arg1, (size_t)arg2);
    case SYS_STAT:
        return sys_stat((const char *)arg1, (struct stat *)arg2);
    case SYS_FSTAT:
        return sys_fstat((int)arg1, (struct stat *)arg2);
    case SYS_LINK:
        return sys_link((const char *)arg1, (const char *)arg2);
    case SYS_UNLINK:
        return sys_unlink((const char *)arg1);
    case SYS_GETCWD:
        return sys_getcwd((char *)arg1, (size_t)arg2);
    case SYS_GETTIMEOFDAY:
        return sys_gettimeofday((struct timeval *)arg1, (struct timezone *)arg2);
    case SYS_PIPE:
        return sys_pipe((int *)arg1);
    case SYS_LSEEK:
        return sys_lseek((int)arg1, (long)arg2, (int)arg3);
    case SYS_DUP:
        return sys_dup((int)arg1);
    case SYS_SHUTDOWN:
        sys_shutdown();
        return 0;
    case SYS_REBOOT:
        sys_reboot();
        return 0;
    case SYS_KILL:
        return sys_kill((int)arg1, (int)arg2);
    default:
        printk("Unknown syscall: %lu\n", syscall_number);
        return -1;
    }
}

int sys_read(int fd, char *buf, size_t count)
{
    if (fd < 0 || fd >= MAX_FDS)
        return 0;

    if (!prepare_user_buffer(buf, count, true))
        return -1;

    file_descriptor_t *desc = current_process->fd_table[fd];

    // Handle stdin (fd 0) specially only if it's the console device or not set up
    if (fd == 0)
    {
        // If fd 0 has a real descriptor with an inode, use it
        if (desc && desc->inode)
        {
            if (!fd_can_read(desc))
                return -1;
            uint64_t read = vfs_read(desc->inode, desc->offset, count, (uint8_t *)buf);
            desc->offset += read;
            return clamp_to_int(read);
        }

        // No descriptor or no inode - fall back to keyboard
        if (desc && !fd_can_read(desc))
            return -1;

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
        if (read == 0 && !keyboard_has_char())
            keyboard_clear_modifiers();
        return clamp_to_int(read);
    }

    if (!desc || !desc->inode)
        return 0;
    if (!fd_can_read(desc))
        return -1;

    uint64_t read = vfs_read(desc->inode, desc->offset, count, (uint8_t *)buf);
    desc->offset += read;
    return clamp_to_int(read);
}

int sys_open(const char *path, int flags)
{
    if (!path || !*path)
        return -1;
    const bool want_write = (flags & O_WRONLY) || (flags & O_RDWR);
    char abs_path[VFS_MAX_PATH];
    resolve_user_path(path, abs_path, sizeof(abs_path));
    int fd = -1;
    for (int i = 3; i < MAX_FDS; i++)
    {
        if (current_process->fd_table[i] == nullptr)
        {
            fd = i;
            break;
        }
    }
    if (fd == -1)
        return -1;

    vfs_inode_t *inode = vfs_resolve_path(abs_path);
    if (!inode && (flags & O_CREATE))
    {
        if (vfs_mknod(abs_path, VFS_FILE, 0) == 0)
            inode = vfs_resolve_path(abs_path);
    }
    if (!inode)
        return -1;

    // Initialize ref count for dup() support
    if (inode->ref == 0)
        inode->ref = 1;

    if ((flags & O_TRUNC) && (inode->flags & VFS_FILE))
    {
        if (!want_write)
        {
            vfs_close(inode);
            if (inode != vfs_root)
                kfree(inode);
            return -1;
        }
        if (vfs_truncate(inode) != 0)
        {
            vfs_close(inode);
            if (inode != vfs_root)
                kfree(inode);
            return -1;
        }
    }

    file_descriptor_t *desc = kmalloc(sizeof(file_descriptor_t));
    if (!desc)
    {
        vfs_close(inode);
        if (inode != vfs_root)
            kfree(inode);
        return -1;
    }

    desc->inode = inode;
    desc->offset = 0;
    if (flags & O_APPEND)
        desc->offset = inode->size;
    desc->flags = flags;
    desc->ref = 1;
    current_process->fd_table[fd] = desc;

    vfs_open(inode);
    return fd;
}

int sys_ioctl(int fd, int request, void *arg)
{
    size_t arg_size = 0;
    switch (request)
    {
    case TIOCGWINSZ:
        arg_size = sizeof(struct winsize);
        break;
    case FB_IOCTL_GET_WIDTH:
    case FB_IOCTL_GET_HEIGHT:
    case FB_IOCTL_GET_PITCH:
        arg_size = sizeof(uint32_t);
        break;
    case FB_IOCTL_GET_FBADDR:
        arg_size = sizeof(uint64_t);
        break;
    default:
        break;
    }

    if (arg_size > 0 && !prepare_user_buffer(arg, arg_size, true))
        return -1;

    if (fd < 0 || fd >= MAX_FDS)
        return -1;

    file_descriptor_t *desc = current_process->fd_table[fd];
    if (!desc || !desc->inode)
        return -1;

    return vfs_ioctl(desc->inode, request, arg);
}

void *sys_mmap(void *addr, size_t length, int prot, int flags, int fd, size_t offset)
{
    (void)prot;
    if (length == 0)
        return MAP_FAILED;

    // Only support shared mappings of /dev/fb0 for now.
    if (!(flags & MAP_SHARED))
        return MAP_FAILED;

    if (fd < 0 || fd >= MAX_FDS)
        return MAP_FAILED;

    file_descriptor_t *desc = current_process->fd_table[fd];
    if (!desc || !desc->inode)
        return MAP_FAILED;

    // Require this to be the framebuffer device.
    struct limine_framebuffer *fb = framebuffer_current();
    if (!fb || desc->inode->device != fb)
        return MAP_FAILED;

    uint64_t fb_size = (uint64_t)fb->pitch * fb->height;
    if (offset >= fb_size)
        return MAP_FAILED;

    uint64_t map_len = length;
    if (offset + map_len > fb_size)
        map_len = fb_size - offset;

    uint64_t page_len = align_up(map_len, PAGE_SIZE);
    uint64_t page_offset = offset & ~(PAGE_SIZE - 1);
    uint64_t in_page_delta = offset - page_offset;
    uint64_t total_len = page_len + in_page_delta;

    // Choose a base address if none provided.
    uint64_t base = (uint64_t)addr;
    if (base == 0)
        base = 0x4000000000; // simple search base for mmaps

    base = align_up(base, PAGE_SIZE);

    // Ensure no overlap with existing VMAs.
    while (true)
    {
        bool overlap = false;
        vm_area_t *area;
        list_for_each_entry(area, &current_process->vm_areas, list)
        {
            if (!(base + total_len <= area->start || base >= area->end))
            {
                overlap = true;
                base = align_up(area->end, PAGE_SIZE);
                break;
            }
        }
        if (!overlap)
            break;
        if (base >= 0x7FFFFFFFF000)
            return MAP_FAILED;
    }

    uint64_t fb_addr = (uint64_t)fb->address;
    uint64_t phys_base = (fb_addr >= g_hhdm_offset) ? (fb_addr - g_hhdm_offset) : fb_addr;

    uint64_t virt = base;
    uint64_t phys = phys_base + page_offset;
    uint64_t bytes_mapped = 0;

    while (bytes_mapped < total_len)
    {
        vmm_map_page(current_process->pml4, virt, phys, PTE_PRESENT | PTE_USER | PTE_WRITABLE);
        virt += PAGE_SIZE;
        phys += PAGE_SIZE;
        bytes_mapped += PAGE_SIZE;
    }

    vm_area_add(current_process, base, base + total_len, VMA_READ | VMA_WRITE | VMA_USER | VMA_MMAP);

    return (void *)(base + in_page_delta);
}

int sys_munmap(void *addr, size_t length)
{
    if (!addr || length == 0)
        return -1;

    uint64_t start = (uint64_t)addr & ~(PAGE_SIZE - 1);
    uint64_t end = start + align_up(length, PAGE_SIZE);

    vm_area_t *area;
    bool found = false;
    list_for_each_entry(area, &current_process->vm_areas, list)
    {
        if (area->start == start && area->end == end && (area->flags & VMA_MMAP))
        {
            found = true;
            break;
        }
    }
    if (!found)
        return -1;

    for (uint64_t va = start; va < end; va += PAGE_SIZE)
        vmm_unmap_page(current_process->pml4, va);

    vm_area_t *tmp;
    list_for_each_entry_safe(area, tmp, &current_process->vm_areas, list)
    {
        if (area->start == start && area->end == end && (area->flags & VMA_MMAP))
        {
            list_del(&area->list);
            kfree(area);
            current_process->vm_area_count--;
            break;
        }
    }
    return 0;
}

int sys_pipe(int pipefd[2])
{
    if (!pipefd)
        return -1;
    if (!prepare_user_buffer(pipefd, 2 * sizeof(int), true))
        return -1;

    // Find two free file descriptors
    int read_fd = -1, write_fd = -1;
    for (int i = 3; i < MAX_FDS && (read_fd == -1 || write_fd == -1); i++)
    {
        if (current_process->fd_table[i] == nullptr)
        {
            if (read_fd == -1)
                read_fd = i;
            else
                write_fd = i;
        }
    }

    if (read_fd == -1 || write_fd == -1)
        return -1; // No free file descriptors

    // Create the pipe
    vfs_inode_t *read_inode = nullptr;
    vfs_inode_t *write_inode = nullptr;
    if (pipe_alloc(&read_inode, &write_inode) != 0)
        return -1;

    // Allocate file descriptors
    file_descriptor_t *read_desc = kmalloc(sizeof(file_descriptor_t));
    if (!read_desc)
    {
        kfree(read_inode);
        kfree(write_inode);
        return -1;
    }

    file_descriptor_t *write_desc = kmalloc(sizeof(file_descriptor_t));
    if (!write_desc)
    {
        kfree(read_desc);
        kfree(read_inode);
        kfree(write_inode);
        return -1;
    }

    // Set up read descriptor
    read_desc->inode = read_inode;
    read_desc->offset = 0;
    read_desc->flags = O_RDONLY;
    read_desc->ref = 1;

    // Set up write descriptor
    write_desc->inode = write_inode;
    write_desc->offset = 0;
    write_desc->flags = O_WRONLY;
    write_desc->ref = 1;

    // Install in process fd table
    current_process->fd_table[read_fd] = read_desc;
    current_process->fd_table[write_fd] = write_desc;

    // Return fds to user
    pipefd[0] = read_fd;
    pipefd[1] = write_fd;

    return 0;
}

int sys_close(int fd)
{
    if (fd < 0 || fd >= MAX_FDS)
        return -1;
    file_descriptor_t *desc = current_process->fd_table[fd];
    if (!desc)
        return -1;

    // Clear this fd slot first
    current_process->fd_table[fd] = nullptr;

    // Decrement file descriptor ref count
    if (desc->ref > 1)
    {
        desc->ref--;
        return 0; // Other fds still reference this descriptor
    }

    // Last reference to this descriptor - close the inode
    if (desc->inode && desc->inode != vfs_root)
    {
        // Only close and free inode when its ref count reaches 0
        if (desc->inode->ref <= 1)
        {
            vfs_close(desc->inode);
            kfree(desc->inode);
        }
        else
        {
            desc->inode->ref--;
        }
    }
    kfree(desc);
    return 0;
}

long sys_lseek(int fd, long offset, int whence)
{
    if (fd < 3 || fd >= MAX_FDS)
        return -1;
    file_descriptor_t *desc = current_process->fd_table[fd];
    if (!desc || !desc->inode)
        return -1;

    // Pipes are not seekable
    if (desc->inode->flags == VFS_PIPE)
        return -1;

    long new_offset;
    switch (whence)
    {
    case 0: // SEEK_SET
        new_offset = offset;
        break;
    case 1: // SEEK_CUR
        new_offset = (long)desc->offset + offset;
        break;
    case 2: // SEEK_END
        new_offset = (long)desc->inode->size + offset;
        break;
    default:
        return -1;
    }

    if (new_offset < 0)
        return -1;

    desc->offset = (uint64_t)new_offset;
    return new_offset;
}

int sys_dup(int oldfd)
{
    if (oldfd < 0 || oldfd >= MAX_FDS)
        return -1;
    file_descriptor_t *old_desc = current_process->fd_table[oldfd];
    if (!old_desc)
        return -1;

    // Find lowest available fd (per POSIX, starts from 0)
    int newfd = -1;
    for (int i = 0; i < MAX_FDS; i++)
    {
        if (current_process->fd_table[i] == nullptr)
        {
            newfd = i;
            break;
        }
    }
    if (newfd == -1)
        return -1;

    // Share the file descriptor (both fds point to same descriptor)
    // This ensures they share the same file offset per POSIX semantics
    old_desc->ref++;
    current_process->fd_table[newfd] = old_desc;
    return newfd;
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

    // Ensure the kernel buffer is marked accessible and copy out.
#ifdef KASAN
    kasan_unpoison_range(d, sizeof(vfs_dirent_t));
#endif
    copy_to_user(dent, d, sizeof(vfs_dirent_t));
    kfree(d);
    desc->offset++;
    return 1; // Success
}

void sys_shutdown()
{
    outw(0x604, 0x2000);  // qemu
    outw(0x4004, 0x3400); // VirtualBox
    outw(0xB004, 0x2000); // Bochs
    outw(0x600, 0x34);    // Cloud hypervisors

    hlt();
}

void sys_reboot()
{
    uint8_t good = 0x02;
    while (good & 0x02)
        good = inb(0x64);
    outb(0x64, 0xFE);
}