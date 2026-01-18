#include <sys/syscall.h>
#include <arch/x86_64/cpu.h>
#include <arch/x86_64/gdt.h>
#include <drivers/terminal.h>
#include <lib/elf.h>
#include <mem/vmm.h>
#include <mem/pmm.h>
#include <task/process.h>
#include <drivers/keyboard.h>
#include <stdint.h>
#include <lib/string.h>
#include <mem/heap.h>
#include <drivers/framebuffer.h>
#include <arch/x86_64/apic.h>
#include <sys/mman.h>
#include <lib/util.h>
#include <sys/fcntl.h>
#include <arch/x86_64/port_io.h>
#include <fs/vfs.h>
#include <sys/time.h>
#include <drivers/tsc.h>
#include <lib/path.h>
#include <fs/pipe.h>
#include <net/socket.h>
#include <net/ethernet.h>
#include <net/ipv4.h>
#include <net/tcp.h>
#include <net/udp.h>
#include <net/icmp.h>
#include <net/helpers.h>
#include <net/arp.h>
#include <net/network.h>
#include <arpa/inet.h>
#include <debug.h>

extern void syscall_entry(void);
extern void fork_return(void);
extern void fork_child_trampoline(void);

#define TIMER_TICK_MS 10
#define EXEC_MAX_ARGS 16
#define EXEC_MAX_ARG_LEN 128

#ifdef TEST_MODE
extern volatile const char* g_current_test_name;
volatile uint64_t test_syscall_count = 0;
volatile uint64_t test_syscall_last_num = 0;
volatile uint64_t test_syscall_last_arg1 = 0;
#endif

uint8_t bootstrap_stack[4096];

int sys_close(int fd);
int sys_readdir(int fd, vfs_dirent_t* dent);
int64_t sys_sbrk(int64_t increment);
void sys_exit(int code);
int sys_wait(int* status);
int sys_getpid(void);
int sys_read(int fd, char* buf, size_t count);
int sys_write(int fd, const char* buf, size_t count);
int sys_exec(const char* path, struct syscall_regs* regs);
int sys_execve(const char* path, const char* const argv[], const char* const envp[], struct syscall_regs* regs);
int sys_spawn(const char* path);
int sys_fork(struct syscall_regs* regs);
int sys_chdir(const char* path);
int sys_sleep(uint64_t milliseconds);
int sys_usleep(uint64_t usec);
int sys_mknod(const char* path, int mode, int dev);
int sys_ioctl(int fd, int request, void* arg);
int sys_open(const char* path, int flags);
void* sys_mmap(void* addr, size_t length, int prot, int flags, int fd, size_t offset);
int sys_munmap(void* addr, size_t length);
int sys_stat(const char* path, struct stat* st);
int sys_fstat(int fd, struct stat* st);
int sys_link(const char* oldpath, const char* newpath);
int sys_unlink(const char* path);
int sys_gettimeofday(struct timeval* tv, struct timezone* tz);
int sys_pipe(int pipefd[2]);
long sys_lseek(int fd, long offset, int whence);
int sys_dup(int oldfd);
int sys_kill(int pid, int sig);
void sys_shutdown();
void sys_reboot();
static void socket_inode_close(vfs_inode_t* node);
static uint64_t socket_inode_read(const vfs_inode_t* node, uint64_t offset, uint64_t size, uint8_t* buffer);
int sys_bind(int fd, const struct sockaddr* addr, size_t addrlen);
int sys_listen(int fd, int backlog);
int sys_accept(int fd, struct sockaddr* addr, size_t addrlen);
int sys_sendto(int fd, const void* buf, size_t len, int flags,
               const struct sockaddr* dest_addr, socklen_t addrlen);
int sys_recvfrom(int fd, void* buf, size_t len, int flags,
                 struct sockaddr* src_addr, socklen_t* addrlen);

static struct inode_operations socket_iops = {
    .read = socket_inode_read,
    .write = nullptr,
    .truncate = nullptr,
    .open = nullptr,
    .close = socket_inode_close,
    .ioctl = nullptr,
    .readdir = nullptr,
    .finddir = nullptr,
    .clone = nullptr,
    .mknod = nullptr,
    .link = nullptr,
    .unlink = nullptr,
    .stat = nullptr,
};

/**
 * Check if a user pointer is writable within the current thread's context.
 * Returns true if the pointer is valid and writable, false otherwise.
 */
static bool user_ptr_write_ok(const void* dst, size_t size, const char* op)
{
    if (!dst)
        return false;
    const thread_t* t = get_current_thread();
    const bool userish = (t != nullptr && t->is_user);
    if (!userish)
        return true;
    const uintptr_t addr = (uintptr_t)dst;
    const uintptr_t end = addr + size;
    if (end < addr)
        return false;

    const uintptr_t user_top = g_hhdm_offset ? g_hhdm_offset : 0x0000800000000000ull;
    const bool in_kernel = (addr >= user_top) || (end > user_top);

    const uintptr_t ktop = t->kstack_top;
    const uintptr_t kbase = (ktop != 0) ? (ktop - KSTACK_SIZE) : 0;
    const bool in_kstack = (ktop != 0) && (addr < ktop) && (end > kbase);

    if (in_kernel || in_kstack)
    {
        const process_t* p = get_current_process();
        printk("%s: bad dst=%p size=%zu pid=%d tid=%d in_kernel=%d in_kstack=%d ret=%p\n",
               op ? op : "user_ptr_write",
               dst,
               size,
               p != nullptr ? p->pid : -1,
               t->tid,
               in_kernel,
               in_kstack,
               __builtin_return_address(0));
        return false;
    }
    return true;
}

static bool copy_to_user(void* dst, const void* src, size_t size)
{
    if (!dst || !src)
        return false;
    if (!user_ptr_write_ok(dst, size, "copy_to_user"))
        return false;
    memcpy(dst, src, size);
    return true;
}

static bool fd_can_read(const file_descriptor_t* desc)
{
    if (!desc)
        return false;
    const int mode = desc->flags & (O_WRONLY | O_RDWR);
    return mode != O_WRONLY;
}

static bool fd_can_write(const file_descriptor_t* desc)
{
    if (!desc)
        return false;
    const int mode = desc->flags & (O_WRONLY | O_RDWR);
    return mode == O_WRONLY || mode == O_RDWR || mode == (O_WRONLY | O_RDWR);
}

static void fill_stat_from_inode(const vfs_inode_t* inode, struct stat* st)
{
    if (!inode || !st)
        return;

    // Try to use filesystem-specific stat if available
    if (inode->iops && inode->iops->stat)
    {
        if (inode->iops->stat(inode, st) == 0)
            return;
    }

    // Fallback to generic stat
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

void syscall_set_stack(uint64_t stack)
{
    cpu_t* cpu = get_cpu();
    cpu->kernel_rsp = stack;
    tss_set_stack(stack);
}

static void set_process_name_from_path(process_t* proc, const char* path)
{
    if (!proc || !path)
        return;
    const char* name = path;
    for (const char* p = path; *p; p++)
    {
        if (*p == '/' && p[1])
            name = p + 1;
    }
    path_safe_copy(proc->name, sizeof(proc->name), name);
}

static int copy_in_args(const char* const * argv, char args[EXEC_MAX_ARGS][EXEC_MAX_ARG_LEN])
{
    if (!argv)
        return 0;

    int count = 0;
    while (count < EXEC_MAX_ARGS)
    {
        const char* user_arg = argv[count];
        if (!user_arg)
            break;

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

// ReSharper disable once CppDFAConstantParameter
static void setup_user_stack(uint64_t stack_top,
                             const char args[EXEC_MAX_ARGS][EXEC_MAX_ARG_LEN], int argc, uint64_t* out_rsp)
{
    uint64_t sp = stack_top;
    uint64_t arg_ptrs[EXEC_MAX_ARGS];

    for (int i = argc - 1; i >= 0; i--)
    {
        size_t len = strlen(args[i]) + 1;
        sp -= len;
        memcpy((void*)sp, args[i], len);
        arg_ptrs[i] = sp;
    }

    // Align stack to 16 bytes
    sp &= ~0xFul;

    // argv terminator
    sp -= sizeof(uint64_t);
    *(uint64_t*)sp = 0;

    // argv pointers
    for (int i = argc - 1; i >= 0; i--)
    {
        sp -= sizeof(uint64_t);
        *(uint64_t*)sp = arg_ptrs[i];
    }

    // argc
    sp -= sizeof(uint64_t);
    *(uint64_t*)sp = (uint64_t)argc;

    *out_rsp = sp;
}

// ReSharper disable once CppDFAConstantFunctionResult
// ReSharper disable once CppDFAConstantParameter
static int resolve_user_path(const char* path, char* resolved, size_t size)
{
    if (!resolved || size == 0)
        // ReSharper disable once CppDFAUnreachableCode
        return -1;

    const char* base = (current_process && current_process->cwd[0]) ? current_process->cwd : "/";
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
    cpu_t* cpu = get_cpu();
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

#ifdef TEST_MODE
#define TEST_SYSCALL_LOG(fmt, ...)            \
    do                                        \
    {                                         \
        if (exit_hook)                        \
            printk(fmt, ##__VA_ARGS__);       \
    } while (0)
#else
#define TEST_SYSCALL_LOG(fmt, ...) ((void)0)
#endif

int sys_write(int fd, const char* buf, size_t count)
{
    if (fd < 0 || fd >= MAX_FDS)
        return -1;

    file_descriptor_t* desc = current_process->fd_table[fd];

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
            uint64_t written = vfs_write(desc->inode, desc->offset, count, (uint8_t*)buf);
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

    uint64_t written = vfs_write(desc->inode, desc->offset, count, (uint8_t*)buf);
    desc->offset += written;
    return clamp_to_int(written);
}

void sys_exit(int code)
{
    TEST_SYSCALL_LOG("sys_exit: pid=%d code=%d (exit_hook=%p)\n", current_process->pid, code, exit_hook);

    if (exit_hook)
    {
        exit_hook(code);
    }
#ifdef TEST_MODE
    if (exit_hook && current_process->parent)
    {
        printk("sys_exit: child pid=%d code=%d waking parent pid=%d\n",
               current_process->pid,
               code,
               current_process->parent ? current_process->parent->pid : -1);
    }
#endif
    TEST_SYSCALL_LOG("Process %d exited with code %d\n", current_process->pid, code);

    __asm__ volatile("cli");

    // Acquire scheduler lock before modifying thread/process state to prevent races
    spinlock_acquire(&scheduler_lock);

    thread_t* self = current_thread;
    process_t* proc = current_process;
    if (self)
        self->state = THREAD_TERMINATED;

    bool proc_terminated = false;
    if (self && self->is_user)
    {
        proc_terminated = true;
    }
    else if (proc)
    {
        proc_terminated = true;
        thread_t* t;
        list_foreach_entry(t, &proc->threads, list)
        {
            if (t->state != THREAD_TERMINATED)
            {
                proc_terminated = false;
                break;
            }
        }
    }

    process_t* parent = nullptr;
    if (proc && proc_terminated)
    {
        proc->exit_code = code;
        proc->terminated = true;

        process_t* new_parent = init_process ? init_process : kernel_process;
        process_t* p;
        list_foreach_entry(p, &process_list, list)
        {
            if (p && p->parent == proc)
            {
                p->parent = new_parent;
                if (p->terminated)
                    thread_wakeup(new_parent);
            }
        }

        parent = proc->parent;
    }

    spinlock_release(&scheduler_lock);

    // Wake up parent outside the lock (thread_wakeup acquires its own lock)
    // Note: interrupts are still disabled, so thread_wakeup won't be preempted
    if (parent)
        thread_wakeup(parent);

    // schedule() will switch to another thread; interrupts will be re-enabled
    // when the new thread runs. This thread's stack is safe because no IPI
    // can arrive to trigger reaping while we're still using it.
    schedule();
}

int sys_kill(int pid, int sig)
{
    (void)sig; // For now, any signal terminates the process

    // Disable interrupts first. If we end up killing ourselves, we need to keep
    // them disabled until after schedule() to prevent an IPI from triggering a
    // nested schedule that could free our stack while we're still using it.
    uint64_t rflags;
    __asm__ volatile("pushfq; pop %0; cli" : "=r"(rflags));

    // Acquire scheduler lock before accessing process_list and modifying the state
    spinlock_acquire(&scheduler_lock);

    // Find the target process
    process_t* target = nullptr;
    list_item_t* pos;
    list_foreach(pos, &process_list)
    {
        process_t* p = list_entry(pos, process_t, list);
        if (p->pid == pid)
        {
            target = p;
            break;
        }
    }

    if (!target)
    {
        spinlock_release(&scheduler_lock);
        if (rflags & RFLAGS_IF)
            __asm__ volatile("sti");
        return -1; // Process not found
    }

    // Don't allow killing the kernel process or init
    if (target->pid <= 1 || (init_process && target == init_process))
    {
        spinlock_release(&scheduler_lock);
        if (rflags & RFLAGS_IF)
            __asm__ volatile("sti");
        return -1;
    }

    // Mark the process as terminated
    target->exit_code = 128 + sig; // Convention: exit code = 128 + signal number
    target->terminated = true;

    // Terminate all threads of the process
    list_item_t* thread_pos;
    list_foreach(thread_pos, &target->threads)
    {
        thread_t* t = list_entry(thread_pos, thread_t, list);
        t->state = THREAD_TERMINATED;
    }

    process_t* new_parent = init_process ? init_process : kernel_process;
    process_t* p;
    list_foreach_entry(p, &process_list, list)
    {
        if (p && p->parent == target)
        {
            p->parent = new_parent;
            if (p->terminated)
                thread_wakeup(new_parent);
        }
    }

    // Cache parent and check if we killed ourselves before releasing lock
    process_t* parent = target->parent;
    bool killed_self = (target == current_process);

    spinlock_release(&scheduler_lock);

    // Wake up the parent if it's waiting (thread_wakeup acquires its own lock)
    if (parent)
        thread_wakeup(parent);

    // If we killed ourselves, reschedule (interrupts stay disabled)
    if (killed_self)
    {
        schedule();
        // schedule() won't return for a terminated thread
    }

    // Restore interrupt state only if we didn't kill ourselves
    if (rflags & RFLAGS_IF)
        __asm__ volatile("sti");

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
        "push %0\n" // SS
        "push %1\n" // RSP
        "push %2\n" // RFLAGS
        "push %3\n" // CS
        "push %4\n" // RIP
        "xor rdi, rdi\n" // argc = 0
        "xor rsi, rsi\n" // argv = nullptr
        "iretq\n"
        :
        : "r"(user_ss), "r"(stack), "r"(rflags), "r"(user_cs), "r"(entry)
        : "memory", "rdi", "rsi");
}

int sys_spawn(const char* path)
{
    if (!path || !*path)
        return -1;
    TEST_SYSCALL_LOG("sys_spawn: parent pid=%d path=%s\n", current_process ? current_process->pid : -1, path);
    char abs_path[PATH_MAX];
    if (resolve_user_path(path, abs_path, sizeof(abs_path)) != 0)
        // ReSharper disable once CppDFAUnreachableCode
        return -1;

    pml4_t new_pml4 = vmm_new_pml4();
    if (!new_pml4)
    {
        TEST_SYSCALL_LOG("sys_spawn: pid=%d new_pml4 alloc failed\n", current_process ? current_process->pid : -1);
        return -1;
    }

    uint64_t entry_point;
    uint64_t max_vaddr;
    if (!elf_load(abs_path, &entry_point, &max_vaddr, new_pml4))
    {
        TEST_SYSCALL_LOG("sys_spawn: pid=%d elf_load failed path=%s\n", current_process ? current_process->pid : -1,
                         abs_path);
        vmm_destroy_pml4(new_pml4);
        return -1;
    }

    uint64_t stack_top = 0x7FFFFFFFF000;
    uint64_t stack_size = 4 * 4096;
    uint64_t stack_base = stack_top - stack_size;

    for (uint64_t addr = stack_base; addr < stack_top; addr += 4096)
    {
        void* phys = pmm_alloc_page();
        if (!phys)
        {
            TEST_SYSCALL_LOG("sys_spawn: pid=%d stack alloc failed path=%s\n",
                             current_process ? current_process->pid : -1, abs_path);
            vmm_destroy_pml4(new_pml4);
            return -1;
        }
        vmm_map_page(new_pml4, addr, (uint64_t)phys, PTE_PRESENT | PTE_WRITABLE | PTE_USER);
    }

    process_t* proc = process_create(path);
    if (!proc)
    {
        vmm_destroy_pml4(new_pml4);
        return -1;
    }
    set_process_name_from_path(proc, abs_path);
    proc->pml4 = new_pml4;
    proc->parent = current_process;
    proc->heap_end = max_vaddr;

    process_copy_fds(proc, current_process);
    vm_area_add(proc, stack_base, stack_top, VMA_READ | VMA_WRITE | VMA_USER | VMA_STACK);

    thread_t* thread = thread_create(proc, spawn_trampoline, true);
    if (!thread)
    {
        process_destroy(proc);
        return -1;
    }
    thread->user_entry = entry_point;
    thread->user_stack = stack_top;

    TEST_SYSCALL_LOG("sys_spawn: created pid=%d tid=%d entry=%lx stack=%lx parent=%d\n",
                     proc->pid,
                     thread ? thread->tid : -1,
                     (unsigned long)entry_point,
                     (unsigned long)stack_top,
                     current_process ? current_process->pid : -1);
    return proc->pid;
}

int sys_fork(struct syscall_regs* regs)
{
    if (!regs)
        return -1;

    // Copy Address Space
    pml4_t child_pml4 = vmm_copy_pml4(current_process->pml4);
    if (!child_pml4)
        return -1;


    // Create Process
    process_t* child_proc = process_create(current_process->name);
    if (!child_proc)
    {
        vmm_destroy_pml4(child_pml4);
        return -1;
    }

    child_proc->pml4 = child_pml4;
    child_proc->parent = current_process;
    child_proc->heap_end = current_process->heap_end;

    process_copy_fds(child_proc, current_process);
    vm_area_clone(child_proc, current_process);

    // Create Thread
    thread_t* child_thread = thread_create(child_proc, nullptr, true);
    if (!child_thread)
    {
        process_destroy(child_proc);
        return -1;
    }

    // Setup Child Stack
    // Place the context-switch frame near the top of the kernel stack (like thread_create()).
    // We must also keep syscall_regs immediately above the context so fork_child_trampoline
    // starts with RSP == child_regs after switch_to's pops+ret.
    uint64_t ctx_addr = child_thread->kstack_top;
    ctx_addr -= KSTACK_SYSCALL_HEADROOM;
    ctx_addr -= sizeof(struct syscall_regs);
    ctx_addr -= sizeof(struct context);
    ctx_addr &= ~0xFULL; // keep 16-byte alignment so child_regs ends up 8 mod 16

    struct context* child_ctx = (struct context*)ctx_addr;
    struct syscall_regs* child_regs = (struct syscall_regs*)(ctx_addr + sizeof(struct context));

    *child_regs = *regs; // Copy user registers
    memset(child_ctx, 0, sizeof(struct context));
    child_ctx->rip = (uint64_t)fork_child_trampoline;

    child_thread->context = child_ctx;
    child_thread->rsp = (uint64_t)child_ctx;
    cpu_t* cpu = get_cpu();
    child_thread->saved_user_rsp = cpu->user_rsp; // Inherit user stack pointer


#ifdef TEST_MODE
    if (exit_hook)
    {
        // Capture a snapshot of the child stack layout before it ever runs.
        const uint64_t ctx_addr = (uint64_t)child_ctx;
        const uint64_t regs_addr = (uint64_t)child_regs;
        const uint64_t frame_words[4] = {
            *((uint64_t*)ctx_addr),
            *((uint64_t*)(ctx_addr + 8)),
            *((uint64_t*)(ctx_addr + 16)),
            *((uint64_t*)(ctx_addr + 24)),
        };
        printk("sys_fork: child pid=%d tid=%d rsp=0x%lx ktop=0x%lx saved_user_rsp=0x%lx\n",
               child_proc->pid,
               child_thread->tid,
               child_thread->rsp,
               child_thread->kstack_top,
               child_thread->saved_user_rsp);
        printk("sys_fork: child regs @0x%lx rcx=0x%lx r11=0x%lx\n",
               regs_addr,
               child_regs->rcx,
               child_regs->r11);
        printk("sys_fork: child ctx @0x%lx rip=0x%lx first_qwords=[%lx %lx %lx %lx]\n",
               ctx_addr,
               child_ctx->rip,
               frame_words[0],
               frame_words[1],
               frame_words[2],
               frame_words[3]);
    }
#endif

    TEST_SYSCALL_LOG("sys_fork: parent pid=%d child pid=%d\n", current_process->pid, child_proc->pid);

    return child_proc->pid;
}

int sys_getpid(void)
{
    return current_process->pid;
}

int sys_wait(int* status)
{
#ifdef TEST_MODE
    printk("sys_wait: pid=%d waiting...\n", current_process ? current_process->pid : -1);
#endif
    while (1)
    {
        uint64_t rflags;
        SPIN_LOCK_INT_SAVE(scheduler_lock, rflags);

        bool has_children = false;
        process_t* found = nullptr;
        bool has_unreapable_zombie = false;
        process_t* p;
        list_foreach_entry(p, &process_list, list)
        {
            if (p->parent == current_process)
            {
                has_children = true;
                if (p->terminated)
                {
                    if (process_can_reap_locked(p))
                    {
                        found = p;
                        break;
                    }
                    has_unreapable_zombie = true;
                }
            }
        }

        if (found)
        {
            int code = found->exit_code;
            if (status && (uint64_t)status < 0x800000000000)
            {
                copy_to_user(status, &code, sizeof(int));
            }
            int pid = found->pid;

            SPIN_UNLOCK_INT_RESTORE(scheduler_lock, rflags);
            process_reap(found);
            TEST_SYSCALL_LOG("sys_wait: pid=%d reaped child=%d code=%d\n", current_process->pid, pid, code);
            return pid;
        }

        if (!has_children)
        {
            SPIN_UNLOCK_INT_RESTORE(scheduler_lock, rflags);
            return -1;
        }
        if (has_unreapable_zombie)
        {
            SPIN_UNLOCK_INT_RESTORE(scheduler_lock, rflags);
            yield();
            continue;
        }
        TEST_SYSCALL_LOG("sys_wait: pid=%d sleeping for child\n", current_process->pid);
        thread_sleep(current_process, &scheduler_lock);
        SPIN_UNLOCK_INT_RESTORE(scheduler_lock, rflags);
    }
}

int sys_exec(const char* path, struct syscall_regs* regs)
{
    // Add a null terminator so copy_in_args stops after the single path entry.
    const char* argv[2] = {path, nullptr};
    return sys_execve(path, argv, nullptr, regs);
}

int sys_execve(const char* path, const char* const argv[], [[maybe_unused]] const char* const envp[],
               struct syscall_regs* regs)
{
    if (!path || !*path)
        return -1;

    TEST_SYSCALL_LOG("sys_execve: enter pid=%d path_ptr=%p argv_ptr=%p envp_ptr=%p\n",
                     current_process ? current_process->pid : -1,
                     path,
                     argv,
                     envp);

    char abs_path[PATH_MAX];
    if (resolve_user_path(path, abs_path, sizeof(abs_path)) != 0)
        // ReSharper disable once CppDFAUnreachableCode
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

    TEST_SYSCALL_LOG("sys_execve: pid=%d path=%s argc=%d argv_ptr=%p first_arg=%s\n",
                     current_process ? current_process->pid : -1,
                     abs_path,
                     argc,
                     argv,
                     (argc > 0) ? args[0] : "<none>");

    pml4_t old_pml4 = current_process->pml4;
    pml4_t new_pml4 = vmm_new_pml4();
    if (!new_pml4)
    {
        TEST_SYSCALL_LOG("sys_execve: pid=%d new_pml4 alloc failed path=%s\n",
                         current_process ? current_process->pid : -1, abs_path);
        return -1;
    }

    uint64_t entry_point;
    uint64_t max_vaddr;
    if (!elf_load(abs_path, &entry_point, &max_vaddr, new_pml4))
    {
        TEST_SYSCALL_LOG("sys_execve: pid=%d elf_load failed path=%s\n", current_process ? current_process->pid : -1,
                         abs_path);
        vmm_destroy_pml4(new_pml4);
        return -1;
    }

    uint64_t stack_top = 0x7FFFFFFFF000;
    uint64_t stack_size = 4 * 4096;
    uint64_t stack_base = stack_top - stack_size;

    for (uint64_t addr = stack_base; addr < stack_top; addr += 4096)
    {
        void* phys = pmm_alloc_page();
        if (!phys)
        {
            TEST_SYSCALL_LOG("sys_execve: pid=%d stack alloc failed path=%s\n",
                             current_process ? current_process->pid : -1, abs_path);
            vmm_destroy_pml4(new_pml4);
            return -1;
        }
        vmm_map_page(new_pml4, addr, (uint64_t)phys, PTE_PRESENT | PTE_WRITABLE | PTE_USER);
    }

    uint64_t user_rsp = stack_top;
    current_process->pml4 = new_pml4;
    vmm_switch_pml4(new_pml4);
    setup_user_stack(stack_top, args, argc, &user_rsp);

    current_process->heap_end = max_vaddr;
    set_process_name_from_path(current_process, abs_path);
    regs->rcx = entry_point;
    get_cpu()->user_rsp = user_rsp;

    if (old_pml4 && old_pml4 != new_pml4)
        vmm_destroy_pml4(old_pml4);

    TEST_SYSCALL_LOG("sys_execve: pid=%d entry=%lx rsp=%lx\n",
                     current_process ? current_process->pid : -1,
                     (unsigned long)entry_point,
                     (unsigned long)user_rsp);
    return 0;
}

int sys_chdir(const char* path)
{
    if (!path || !*path)
        return -1;
    char abs_path[PATH_MAX];
    resolve_user_path(path, abs_path, sizeof(abs_path));

    vfs_inode_t* node = vfs_resolve_path(abs_path);
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

int sys_getcwd(char* buf, size_t size)
{
    if (!buf || size == 0)
        return -1;
    const char* cwd = (current_process && current_process->cwd[0]) ? current_process->cwd : "/";


    const size_t len = strlen(cwd);
    if (len + 1 > size)
        return -1;
    if (!user_ptr_write_ok(buf, len + 1, "sys_getcwd"))
        return -1;
    memcpy(buf, cwd, len + 1);


    return 0;
}

int sys_gettimeofday(struct timeval* tv, struct timezone* tz)
{
    uint64_t ns = tsc_nanos();
    if (ns == 0)
        ns = scheduler_ticks * (1000000000ull / TIMER_FREQUENCY_HZ);

    if (tv)
    {
        if (!user_ptr_write_ok(tv, sizeof(*tv), "sys_gettimeofday"))
            return -1;
        tv->tv_sec = (int64_t)(ns / 1000000000ull);
        tv->tv_usec = (int64_t)((ns % 1000000000ull) / 1000ull);
    }
    if (tz)
    {
        if (!user_ptr_write_ok(tz, sizeof(*tz), "sys_gettimeofday"))
            return -1;
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

// ReSharper disable once CppDFAConstantFunctionResult
int sys_usleep(uint64_t usec)
{
    if (usec == 0)
        return 0;

    constexpr uint64_t tick_us = 1000000ull / TIMER_FREQUENCY_HZ;
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

int sys_mknod(const char* path, int mode, int dev)
{
    if ((uint64_t)path >= 0x800000000000) // Check if user pointer
        return -1;

    char kpath[PATH_MAX];
    path_safe_copy(kpath, sizeof(kpath), path);
    path_simplify(kpath, sizeof(kpath));

    return vfs_mknod(kpath, mode, dev);
}

int sys_stat(const char* path, struct stat* st)
{
    if (!path || !st)
        return -1;
    if (!user_ptr_write_ok(st, sizeof(*st), "sys_stat"))
        return -1;

    char abs_path[PATH_MAX];
    resolve_user_path(path, abs_path, sizeof(abs_path));

    vfs_inode_t* inode = vfs_resolve_path(abs_path);
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

int sys_link(const char* oldpath, const char* newpath)
{
    if (!oldpath || !newpath || !*oldpath || !*newpath)
        return -1;

    char abs_old[PATH_MAX];
    char abs_new[PATH_MAX];
    resolve_user_path(oldpath, abs_old, sizeof(abs_old));
    resolve_user_path(newpath, abs_new, sizeof(abs_new));

    return vfs_link(abs_old, abs_new);
}

int sys_unlink(const char* path)
{
    if (!path || !*path)
        return -1;

    char abs_path[PATH_MAX];
    resolve_user_path(path, abs_path, sizeof(abs_path));

    // Prevent unlinking the root
    if (strcmp(abs_path, "/") == 0)
        return -1;

    return vfs_unlink(abs_path);
}

int sys_fstat(int fd, struct stat* st)
{
    if (!st || fd < 0 || fd >= MAX_FDS)
        return -1;
    if (!user_ptr_write_ok(st, sizeof(*st), "sys_fstat"))
        return -1;

    file_descriptor_t* desc = current_process->fd_table[fd];
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
            void* phys = pmm_alloc_page();
            if (!phys)
            {
                return -1; // OOM
            }
            vmm_map_page(current_process->pml4, addr, (uint64_t)phys, PTE_PRESENT | PTE_WRITABLE | PTE_USER);
            memset((void*)addr, 0, PAGE_SIZE);
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

int sys_socket(const int domain, const int type, int protocol)
{
    if (domain != PF_INET) return -1;
    if (protocol == 0)
        protocol = (type == SOCK_DGRAM) ? IPPROTO_UDP : IPPROTO_ICMP;

    if ((type == SOCK_DGRAM && protocol != IPPROTO_UDP) ||
        (type == SOCK_RAW && protocol != IPPROTO_ICMP))
    {
        return -1;
    }

    int fd = -1;
    for (int i = 3; i < MAX_FDS; i++)
    {
        if (current_process->fd_table[i] == nullptr)
        {
            fd = i;
            break;
        }
    }
    if (fd == -1) return -1;

    auto const sock = (socket_t*)kzalloc(sizeof(socket_t));
    if (!sock) return -1;
    sock->domain = domain;
    sock->type = type;
    sock->protocol = protocol;
    sock->state = SOCKET_STATE_UNBOUND;
    sock->ref = 1;

    auto const inode = (vfs_inode_t*)kzalloc(sizeof(vfs_inode_t));
    if (!inode)
    {
        kfree(sock);
        return -1;
    }
    inode->flags = VFS_PIPE;
    inode->ref = 1;
    inode->iops = &socket_iops;
    inode->device = sock;

    auto const desc = (file_descriptor_t*)kzalloc(sizeof(file_descriptor_t));
    if (!desc)
    {
        kfree(inode);
        kfree(sock);
        return -1;
    }
    desc->inode = inode;
    desc->offset = 0;
    desc->flags = O_RDWR;
    desc->ref = 1;

    current_process->fd_table[fd] = desc;
    socket_register(sock);
    return fd;
}

int sys_bind(const int fd, const struct sockaddr* addr, const size_t addrlen)
{
    if (fd < 0 || fd >= MAX_FDS) return -1;
    if (!addr) return -1;
    if (addrlen < sizeof(struct sockaddr_in)) return -1;

    file_descriptor_t* desc = current_process->fd_table[fd];
    if (!desc || !desc->inode) return -1;
    if (desc->inode->iops != &socket_iops) return -1;

    auto const sock = (socket_t*)desc->inode->device;
    if (!sock) return -1;
    if (sock->state != SOCKET_STATE_UNBOUND) return -1;

    struct sockaddr_in in = {0};
    memcpy(&in, addr, sizeof(in));
    if (in.sin_family != AF_INET) return -1;

    uint16_t port = 0;
    if (socket_assign_port(sock, in.sin_addr, in.sin_port, &port) != 0)
        return -1;

    memcpy(sock->local.ip, in.sin_addr, sizeof(sock->local.ip));
    sock->local.port = port;
    sock->state = SOCKET_STATE_BOUND;
    return 0;
}

int sys_listen(const int fd, const int backlog)
{
    if (fd < 0 || fd >= MAX_FDS) return -1;
    if (backlog < 0) return -1;

    file_descriptor_t* desc = current_process->fd_table[fd];
    if (!desc || !desc->inode) return -1;
    if (desc->inode->iops != &socket_iops) return -1;

    auto const sock = (socket_t*)desc->inode->device;
    if (!sock) return -1;
    if (sock->type != SOCK_STREAM || sock->protocol != IPPROTO_TCP) return -1;
    if (sock->state != SOCKET_STATE_BOUND && sock->state != SOCKET_STATE_LISTENING) return -1;

    sock->backlog = (backlog > 0) ? backlog : 1;
    sock->state = SOCKET_STATE_LISTENING;
    return 0;
}

int sys_accept(const int fd, struct sockaddr* addr, const size_t addrlen)
{
    if (fd < 0 || fd >= MAX_FDS) return -1;

    file_descriptor_t* desc = current_process->fd_table[fd];
    if (!desc || !desc->inode) return -1;
    if (desc->inode->iops != &socket_iops) return -1;

    socket_t* listener = (socket_t*)desc->inode->device;
    if (!listener) return -1;
    if (listener->type != SOCK_STREAM || listener->protocol != IPPROTO_TCP) return -1;
    if (listener->state != SOCKET_STATE_LISTENING) return -1;
    if (addr && addrlen < sizeof(struct sockaddr_in)) return -1;
    if (addr && !user_ptr_write_ok(addr, sizeof(struct sockaddr_in), "sys_accept")) return -1;

    socket_t* child = nullptr;
    spinlock_acquire(&listener->accept_lock);
    while (list_empty(&listener->accept_queue))
        thread_sleep(listener, &listener->accept_lock);
    child = list_entry(listener->accept_queue.next, socket_t, accept_list);
    list_del(&child->accept_list);
    if (listener->accept_queue_len > 0)
        listener->accept_queue_len--;
    spinlock_release(&listener->accept_lock);

    int new_fd = -1;
    for (int i = 3; i < MAX_FDS; i++)
    {
        if (current_process->fd_table[i] == nullptr)
        {
            new_fd = i;
            break;
        }
    }
    if (new_fd == -1)
    {
        socket_unregister(child);
        kfree(child);
        return -1;
    }

    auto const inode = (vfs_inode_t*)kzalloc(sizeof(vfs_inode_t));
    if (!inode)
    {
        socket_unregister(child);
        kfree(child);
        return -1;
    }
    inode->flags = VFS_PIPE;
    inode->ref = 1;
    inode->iops = &socket_iops;
    inode->device = child;

    auto const new_desc = (file_descriptor_t*)kzalloc(sizeof(file_descriptor_t));
    if (!new_desc)
    {
        kfree(inode);
        socket_unregister(child);
        kfree(child);
        return -1;
    }
    new_desc->inode = inode;
    new_desc->offset = 0;
    new_desc->flags = O_RDWR;
    new_desc->ref = 1;
    current_process->fd_table[new_fd] = new_desc;

    if (addr)
    {
        struct sockaddr_in out = {0};
        out.sin_family = AF_INET;
        out.sin_port = child->remote.port;
        memcpy(out.sin_addr, child->remote.ip, sizeof(out.sin_addr));
        if (!copy_to_user(addr, &out, sizeof(out)))
        {
            sys_close(new_fd);
            return -1;
        }
    }

    return new_fd;
}

static bool ip_is_zero(const uint8_t ip[static 4])
{
    return ip[0] == 0 && ip[1] == 0 && ip[2] == 0 && ip[3] == 0;
}

/**
 * Compare the destination IP address with the subnet mask and gateway to determine the next hop.
 */
static void select_next_hop(const uint8_t dest_ip[static 4], uint8_t out[static 4])
{
    const uint8_t* my_ip = network_get_my_ip_address();
    const uint8_t* mask = network_get_subnet_mask();
    const uint8_t* gw = network_get_default_gateway();

    const uint8_t* next = dest_ip;
    if (my_ip && mask && gw)
    {
        bool same = true;
        for (int i = 0; i < 4; i++)
        {
            // AND each octet of the destination IP with the subnet mask
            // Do the same thing with our own IP
            // Compare the two. If they are different, we need to use the gateway
            // because we are not on the same subnet
            if ((dest_ip[i] & mask[i]) != (my_ip[i] & mask[i]))
            {
                same = false;
                break;
            }
        }
        if (!same) next = gw;
    }

    memcpy(out, next, 4);
}

int tcp_sendto(const void* buf, const size_t len, const struct sockaddr* dest_addr, socket_t* const sock,
               struct sockaddr_in in)
{
    if (sock->state != SOCKET_STATE_CONNECTED) return -1;
    if ((sock->flags & SOCKET_FLAG_TCP_ESTABLISHED) == 0) return -1;
    if (ip_is_zero(sock->remote.ip) || sock->remote.port == 0) return -1;
    if (dest_addr &&
        (memcmp(sock->remote.ip, in.sin_addr, sizeof(sock->remote.ip)) != 0 ||
            sock->remote.port != in.sin_port))
        return -1;
    if (len == 0) return 0;

    constexpr uint8_t tcp_flags = (uint8_t)(TCP_FLAG_ACK | TCP_FLAG_PSH);
    if (tcp_send_segment(sock, sock->remote.ip, sock->remote.port,
                         sock->tcp_send_next, sock->tcp_recv_next,
                         tcp_flags, (const uint8_t*)buf, len, nullptr) != 0)
        return -1;
    sock->tcp_send_next += (uint32_t)len;
    return clamp_to_int(len);
}

int udp_sendto(const void* buf, const size_t len, socket_t* const sock, struct sockaddr_in in, const uint8_t* my_ip,
               uint8_t src_ip[static 4])
{
    if (in.sin_port == 0) return -1;
    if (sock->state == SOCKET_STATE_UNBOUND)
    {
        uint16_t port = 0;
        if (socket_assign_port(sock, my_ip, 0, &port) != 0)
            return -1;
        memcpy(sock->local.ip, my_ip, sizeof(sock->local.ip));
        sock->local.port = port;
        sock->state = SOCKET_STATE_BOUND;
        memcpy(src_ip, my_ip, sizeof(uint8_t) * 4);
    }

    uint8_t next_hop[4];
    select_next_hop(in.sin_addr, next_hop);
    const struct arp_cache_entry entry = arp_cache_find(next_hop);
    if (entry.ip[0] == 0)
    {
        arp_send_request(next_hop);
        return -1;
    }

    const uint8_t* src_mac = network_get_my_mac_address();
    if (!src_mac) return -1;

    const size_t total_len = sizeof(struct ether_header) + sizeof(struct ipv4_header) +
        sizeof(struct udp_header) + len;
    uint8_t* packet = kmalloc(total_len);
    if (!packet) return -1;

    auto const eth = (struct ether_header*)packet;
    memcpy(eth->dest_host, entry.mac, 6);
    memcpy(eth->src_host, src_mac, 6);
    eth->ether_type = htons(ETHERTYPE_IP);

    auto const ip = (struct ipv4_header*)(packet + sizeof(struct ether_header));
    ip->version = 4;
    ip->ihl = 0x05;
    ip->dscp_ecn = 0;
    ip->total_length = htons(sizeof(struct ipv4_header) + sizeof(struct udp_header) + len);
    ip->identification = 0;
    ip->flags_fragment_offset = 0;
    ip->ttl = 64;
    ip->protocol = IP_PROTOCOL_UDP;
    ip->header_checksum = 0;
    memcpy(ip->source_ip, src_ip, 4);
    memcpy(ip->dest_ip, in.sin_addr, 4);
    ip->header_checksum = checksum(ip, (int)sizeof(struct ipv4_header), 0);

    auto const udp = (struct udp_header*)((uint8_t*)ip + sizeof(struct ipv4_header));
    udp->src_port = sock->local.port;
    udp->dest_port = in.sin_port;
    udp->len = htons(sizeof(struct udp_header) + len);
    udp->checksum = 0;

    uint8_t* payload = (uint8_t*)udp + sizeof(struct udp_header);
    if (len > 0)
        memcpy(payload, buf, len);

    const struct udp_pseudo_header pseudo = {
        .src_ip = {src_ip[0], src_ip[1], src_ip[2], src_ip[3]},
        .dest_ip = {in.sin_addr[0], in.sin_addr[1], in.sin_addr[2], in.sin_addr[3]},
        .zero = 0,
        .protocol = IP_PROTOCOL_UDP,
        .udp_length = udp->len,
    };

    const size_t checksum_len = sizeof(struct udp_pseudo_header) + sizeof(struct udp_header) + len;
    uint8_t* checksum_buf = kmalloc(checksum_len);
    if (!checksum_buf)
    {
        kfree(packet);
        return -1;
    }
    memcpy(checksum_buf, &pseudo, sizeof(struct udp_pseudo_header));
    memcpy(checksum_buf + sizeof(struct udp_pseudo_header), udp, sizeof(struct udp_header));
    if (len > 0)
        memcpy(checksum_buf + sizeof(struct udp_pseudo_header) + sizeof(struct udp_header), payload, len);
    udp->checksum = checksum(checksum_buf, (int)checksum_len, 0);
    kfree(checksum_buf);

    network_send_packet(packet, (uint16_t)total_len);
    kfree(packet);
    return clamp_to_int(len);
}

int icmp_sendto(const void* buf, const size_t len, struct sockaddr_in in, uint8_t src_ip[4])
{
    if (len < sizeof(struct icmp_header)) return -1;

    uint8_t next_hop[4];
    select_next_hop(in.sin_addr, next_hop);
    const struct arp_cache_entry entry = arp_cache_find(next_hop);
    if (entry.ip[0] == 0)
    {
        arp_send_request(next_hop);
        return -1;
    }

    const uint8_t* src_mac = network_get_my_mac_address();
    if (!src_mac)
        return -1;

    const size_t total_len = sizeof(struct ether_header) + sizeof(struct ipv4_header) + len;
    uint8_t* packet = kmalloc(total_len);
    if (!packet)
        return -1;

    auto const eth = (struct ether_header*)packet;
    memcpy(eth->dest_host, entry.mac, 6);
    memcpy(eth->src_host, src_mac, 6);
    eth->ether_type = htons(ETHERTYPE_IP);

    auto const ip = (struct ipv4_header*)(packet + sizeof(struct ether_header));
    ip->version = 4;
    ip->ihl = 0x05;
    ip->dscp_ecn = 0;
    ip->total_length = htons(sizeof(struct ipv4_header) + len);
    ip->identification = 0;
    ip->flags_fragment_offset = 0;
    ip->ttl = 64;
    ip->protocol = IP_PROTOCOL_ICMP;
    ip->header_checksum = 0;
    memcpy(ip->source_ip, src_ip, 4);
    memcpy(ip->dest_ip, in.sin_addr, 4);
    ip->header_checksum = checksum(ip, (int)sizeof(struct ipv4_header), 0);

    uint8_t* icmp_data = (uint8_t*)ip + sizeof(struct ipv4_header);
    if (len > 0)
        memcpy(icmp_data, buf, len);
    auto const icmp = (struct icmp_header*)icmp_data;
    icmp->checksum = 0;
    icmp->checksum = checksum(icmp_data, (int)len, 0);

    network_send_packet(packet, (uint16_t)total_len);
    kfree(packet);
    return clamp_to_int(len);
}

int sys_sendto(const int fd, const void* buf, const size_t len, const int flags,
               const struct sockaddr* dest_addr, const socklen_t addrlen)
{
    (void)flags;
    if (fd < 0 || fd >= MAX_FDS) return -1;
    if (!buf && len > 0) return -1;

    file_descriptor_t* desc = current_process->fd_table[fd];
    if (!desc || !desc->inode) return -1;
    if (desc->inode->iops != &socket_iops) return -1;

    auto const sock = (socket_t*)desc->inode->device;
    if (!sock) return -1;

    const uint8_t* my_ip = network_get_my_ip_address();
    if (!my_ip) return -1;
    uint8_t src_ip[4];
    // If the IP is 0.0.0.0, that is local, so use our own IP
    if (ip_is_zero(sock->local.ip))
        memcpy(src_ip, my_ip, sizeof(src_ip));
    else
        memcpy(src_ip, sock->local.ip, sizeof(src_ip));

    struct sockaddr_in in = {0};
    if (dest_addr)
    {
        if (addrlen < sizeof(struct sockaddr_in)) return -1;
        memcpy(&in, dest_addr, sizeof(in));
        if (in.sin_family != AF_INET) return -1;
    }
    else if (sock->protocol != IPPROTO_TCP || sock->type != SOCK_STREAM)
    {
        return -1;
    }

    if (sock->protocol == IPPROTO_TCP && sock->type == SOCK_STREAM)
        return tcp_sendto(buf, len, dest_addr, sock, in);

    if (sock->protocol == IPPROTO_UDP && sock->type == SOCK_DGRAM)
        return udp_sendto(buf, len, sock, in, my_ip, src_ip);

    if (sock->protocol == IPPROTO_ICMP && sock->type == SOCK_RAW)
        return icmp_sendto(buf, len, in, src_ip);

    return -1;
}

int sys_recvfrom(const int fd, void* buf, const size_t len, const int flags,
                 struct sockaddr* src_addr, socklen_t* addrlen)
{
    if (fd < 0 || fd >= MAX_FDS) return -1;
    if (len == 0) return 0;
    if (!buf) return -1;
    if (!user_ptr_write_ok(buf, len, "sys_recvfrom")) return -1;
    if (src_addr && !user_ptr_write_ok(src_addr, sizeof(struct sockaddr_in), "sys_recvfrom"))
        return -1;
    if (addrlen && !user_ptr_write_ok(addrlen, sizeof(socklen_t), "sys_recvfrom"))
        return -1;

    file_descriptor_t* desc = current_process->fd_table[fd];
    if (!desc || !desc->inode) return -1;
    if (desc->inode->iops != &socket_iops) return -1;

    socket_t* sock = (socket_t*)desc->inode->device;
    if (!sock) return -1;

    const bool block = (flags & MSG_DONTWAIT) == 0;
    socket_rx_packet_t* pkt = socket_rx_pop(sock, block);
    if (!pkt) return -1;

    size_t copy_len = (pkt->len < len) ? pkt->len : len;
    if (copy_len > 0) memcpy(buf, pkt->data, copy_len);

    if (src_addr)
    {
        struct sockaddr_in out = {0};
        out.sin_family = AF_INET;
        out.sin_port = pkt->from.port;
        memcpy(out.sin_addr, pkt->from.ip, sizeof(out.sin_addr));
        if (!copy_to_user(src_addr, &out, sizeof(out)))
        {
            if (pkt->data)
                kfree(pkt->data);
            kfree(pkt);
            return -1;
        }
    }

    if (addrlen)
    {
        socklen_t out_len = sizeof(struct sockaddr_in);
        if (!copy_to_user(addrlen, &out_len, sizeof(out_len)))
        {
            if (pkt->data) kfree(pkt->data);
            kfree(pkt);
            return -1;
        }
    }

    if (pkt->data) kfree(pkt->data);
    kfree(pkt);
    return clamp_to_int(copy_len);
}

static uint64_t socket_inode_read(const vfs_inode_t* node, uint64_t offset, uint64_t size, uint8_t* buffer)
{
    (void)offset;
    if (!node || !buffer) return 0;
    if (size == 0) return 0;

    auto sock = (socket_t*)node->device;
    if (!sock) return 0;

    socket_rx_packet_t* pkt = socket_rx_pop(sock, true);
    if (!pkt) return 0;

    const size_t copy_len = (pkt->len < size) ? pkt->len : size;
    if (copy_len > 0)
        memcpy(buffer, pkt->data, copy_len);

    if (pkt->data) kfree(pkt->data);
    kfree(pkt);
    return copy_len;
}

static void socket_inode_close(vfs_inode_t* node)
{
    if (!node) return;
    auto sock = (socket_t*)node->device;
    if (sock)
    {
        socket_unregister(sock);
        node->device = nullptr;
        kfree(sock);
    }
}

uint64_t syscall_handler(uint64_t syscall_number, uint64_t arg1, uint64_t arg2, uint64_t arg3,
                         struct syscall_regs* regs)
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
        return sys_write((int)arg1, (const char*)arg2, (size_t)arg3);
    case SYS_EXIT:
        sys_exit((int)arg1);
        return 0;
    case SYS_EXEC:
        sys_exec((const char*)arg1, regs);
        return 0;
    case SYS_EXECVE:
        return sys_execve((const char*)arg1, (const char*const *)arg2, (const char*const *)arg3, regs);
    case SYS_FORK:
        return sys_fork(regs);
    case SYS_SPAWN:
        return sys_spawn((const char*)arg1);
    case SYS_WAIT:
        return sys_wait((int*)arg1);
    case SYS_GETPID:
        return sys_getpid();
    case SYS_YIELD:
        yield();
        return 0;
    case SYS_READ:
        return sys_read((int)arg1, (char*)arg2, (size_t)arg3);
    case SYS_SBRK:
        return sys_sbrk((int64_t)arg1);
    case SYS_OPEN:
        return sys_open((const char*)arg1, (int)arg2);
    case SYS_CLOSE:
        return sys_close((int)arg1);
    case SYS_READDIR:
        return sys_readdir((int)arg1, (vfs_dirent_t*)arg2);
    case SYS_CHDIR:
        return sys_chdir((const char*)arg1);
    case SYS_SLEEP:
        return sys_sleep(arg1);
    case SYS_USLEEP:
        return sys_usleep(arg1);
    case SYS_MKNOD:
        return sys_mknod((const char*)arg1, (int)arg2, (int)arg3);
    case SYS_IOCTL:
        return sys_ioctl((int)arg1, (int)arg2, (void*)arg3);
    case SYS_MMAP:
        return (uint64_t)sys_mmap((void*)arg1, (size_t)arg2, (int)arg3, (int)arg4, (int)arg5, (size_t)arg6);
    case SYS_MUNMAP:
        return (uint64_t)sys_munmap((void*)arg1, (size_t)arg2);
    case SYS_STAT:
        return sys_stat((const char*)arg1, (struct stat*)arg2);
    case SYS_FSTAT:
        return sys_fstat((int)arg1, (struct stat*)arg2);
    case SYS_LINK:
        return sys_link((const char*)arg1, (const char*)arg2);
    case SYS_UNLINK:
        return sys_unlink((const char*)arg1);
    case SYS_GETCWD:
        return sys_getcwd((char*)arg1, (size_t)arg2);
    case SYS_GETTIMEOFDAY:
        return sys_gettimeofday((struct timeval*)arg1, (struct timezone*)arg2);
    case SYS_PIPE:
        return sys_pipe((int*)arg1);
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
    case SYS_SOCKET:
        return sys_socket((int)arg1, (int)arg2, (int)arg3);
    case SYS_BIND:
        return sys_bind((int)arg1, (const struct sockaddr*)arg2, (size_t)arg3);
    case SYS_LISTEN:
        return sys_listen((int)arg1, (int)arg2);
    case SYS_ACCEPT:
        return sys_accept((int)arg1, (struct sockaddr*)arg2, (size_t)arg3);
    case SYS_SENDTO:
        return sys_sendto((int)arg1, (const void*)arg2, (size_t)arg3, (int)arg4,
                          (const struct sockaddr*)arg5, (socklen_t)arg6);
    case SYS_RECVFROM:
        return sys_recvfrom((int)arg1, (void*)arg2, (size_t)arg3, (int)arg4,
                            (struct sockaddr*)arg5, (socklen_t*)arg6);
    default:
        panic("Unknown syscall: %lu\n", syscall_number);
        // ReSharper disable once CppDFAUnreachableCode
        __builtin_unreachable();
    }
}

int sys_read(int fd, char* buf, size_t count)
{
    if (fd < 0 || fd >= MAX_FDS)
        return 0;
    if (count == 0)
        return 0;
    if (!user_ptr_write_ok(buf, count, "sys_read"))
        return -1;

    file_descriptor_t* desc = current_process->fd_table[fd];

    // Handle stdin (fd 0) especially only if it's the console device or not set up
    if (fd == 0)
    {
        // If fd 0 has a real descriptor with an inode, use it
        if (desc && desc->inode)
        {
            if (!fd_can_read(desc))
                return -1;
            uint64_t read = vfs_read(desc->inode, desc->offset, count, (uint8_t*)buf);
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

    uint64_t read = vfs_read(desc->inode, desc->offset, count, (uint8_t*)buf);
    desc->offset += read;
    return clamp_to_int(read);
}

int sys_open(const char* path, int flags)
{
    if (!path || !*path)
        return -1;
    const bool want_write = (flags & O_WRONLY) || (flags & O_RDWR);
    char abs_path[PATH_MAX];
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

    vfs_inode_t* inode = vfs_resolve_path(abs_path);
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

    file_descriptor_t* desc = kmalloc(sizeof(file_descriptor_t));
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

int sys_ioctl(int fd, int request, void* arg)
{
    if (fd < 0 || fd >= MAX_FDS)
        return -1;

    file_descriptor_t* desc = current_process->fd_table[fd];
    if (!desc || !desc->inode)
        return -1;

    return vfs_ioctl(desc->inode, request, arg);
}

void* sys_mmap(void* addr, size_t length, int prot, int flags, int fd, size_t offset)
{
    (void)prot;
    if (length == 0)
        return MAP_FAILED;

    // Only support shared mappings of /dev/fb0 for now.
    if (!(flags & MAP_SHARED))
        return MAP_FAILED;

    if (fd < 0 || fd >= MAX_FDS)
        return MAP_FAILED;

    file_descriptor_t* desc = current_process->fd_table[fd];
    if (!desc || !desc->inode)
        return MAP_FAILED;

    // Require this to be the framebuffer device.
    struct limine_framebuffer* fb = framebuffer_current();
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
        vm_area_t* area;
        list_foreach_entry(area, &current_process->vm_areas, list)
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

    return (void*)(base + in_page_delta);
}

int sys_munmap(void* addr, size_t length)
{
    if (!addr || length == 0)
        return -1;

    uint64_t start = (uint64_t)addr & ~(PAGE_SIZE - 1);
    uint64_t end = start + align_up(length, PAGE_SIZE);

    vm_area_t* area;
    bool found = false;
    list_foreach_entry(area, &current_process->vm_areas, list)
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

    vm_area_t* tmp;
    list_foreach_entry_safe(area, tmp, &current_process->vm_areas, list)
    {
        if (!area)
            panic("%s: area is null", __func__);

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
    if (!user_ptr_write_ok(pipefd, sizeof(int) * 2, "sys_pipe"))
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

    vfs_inode_t* read_inode = nullptr;
    vfs_inode_t* write_inode = nullptr;
    if (pipe_alloc(&read_inode, &write_inode) != 0)
        return -1;

    file_descriptor_t* read_desc = kmalloc(sizeof(file_descriptor_t));
    if (!read_desc)
    {
        kfree(read_inode);
        kfree(write_inode);
        return -1;
    }

    file_descriptor_t* write_desc = kmalloc(sizeof(file_descriptor_t));
    if (!write_desc)
    {
        kfree(read_desc);
        kfree(read_inode);
        kfree(write_inode);
        return -1;
    }

    read_desc->inode = read_inode;
    read_desc->offset = 0;
    read_desc->flags = O_RDONLY;
    read_desc->ref = 1;

    write_desc->inode = write_inode;
    write_desc->offset = 0;
    write_desc->flags = O_WRONLY;
    write_desc->ref = 1;

    current_process->fd_table[read_fd] = read_desc;
    current_process->fd_table[write_fd] = write_desc;

    pipefd[0] = read_fd;
    pipefd[1] = write_fd;

    return 0;
}

int sys_close(int fd)
{
    if (fd < 0 || fd >= MAX_FDS)
        return -1;
    file_descriptor_t* desc = current_process->fd_table[fd];
    if (!desc)
        return -1;

    current_process->fd_table[fd] = nullptr;

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
    file_descriptor_t* desc = current_process->fd_table[fd];
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
    file_descriptor_t* old_desc = current_process->fd_table[oldfd];
    if (!old_desc)
        return -1;

    // Find the lowest available fd (per POSIX, starts from 0)
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

int sys_readdir(int fd, vfs_dirent_t* dent)
{
    if (fd < 3 || fd >= MAX_FDS)
        return -1;
    file_descriptor_t* desc = current_process->fd_table[fd];
    if (!desc)
        return -1;

    vfs_dirent_t* d = vfs_readdir(desc->inode, desc->offset);
    if (!d)
        return 0; // End of directory

    if (!copy_to_user(dent, d, sizeof(vfs_dirent_t)))
    {
        kfree(d);
        return -1;
    }
    kfree(d);
    desc->offset++;
    return 1; // Success
}

void sys_shutdown()
{
    outw(0x604, 0x2000); // qemu
    outw(0x4004, 0x3400); // VirtualBox
    outw(0xB004, 0x2000); // Bochs
    outw(0x600, 0x34); // Cloud hypervisors

    hlt();
}

void sys_reboot()
{
    uint8_t good = 0x02;
    while (good & 0x02)
        good = inb(0x64);
    outb(0x64, 0xFE);
}
