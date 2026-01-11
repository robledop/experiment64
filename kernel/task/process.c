#include <process.h>
#include <heap.h>
#include <string.h>
#include <terminal.h>
#include <cpu.h>
#include <vmm.h>
#include <syscall.h>
#include <spinlock.h>
#include <apic.h>
#include <smp.h>
#include <gdt.h>
#include <debug.h>

#define TIME_SLICE_TICKS ((TIME_SLICE_MS * TIMER_FREQUENCY_HZ) / 1000)
static constexpr size_t MAX_CPUS = 32;

list_item_t process_list __attribute__((aligned(16))) = LIST_HEAD_INIT(process_list);
process_t* kernel_process = nullptr;
process_t* init_process = nullptr;
static thread_t* idle_threads[MAX_CPUS] = {nullptr};
static int next_pid = 1;
static int next_tid = 1;
volatile uint64_t scheduler_ticks = 0;

spinlock_t scheduler_lock;
static bool scheduler_ready = false; // Ignore timer ticks until process_init completes

extern void thread_trampoline(void);

[[noreturn]] static void scheduler_loop(void);
static bool thread_is_active_on_any_cpu(thread_t* t);
static void process_destroy_now(process_t* proc);

static inline void thread_list_move_to_tail(thread_t* t)
{
    if (!t || !t->process)
        return;
    list_del(&t->list);
    list_add_tail(&t->list, &t->process->threads);
}

/**
 * Initialize the virtual memory area list for a process.
 * @param proc
 */
void vm_area_init(process_t* proc)
{
    if (!proc)
        return;
    list_init_head(&proc->vm_areas);
    proc->vm_area_count = 0;
}

/**
 * Add a virtual memory area to a process.
 * @param proc Process to add the area to
 * @param start Start address
 * @param end End address
 * @param flags Protection flags
 * @return
 */
vm_area_t* vm_area_add(process_t* proc, uint64_t start, uint64_t end, uint32_t flags)
{
    if (!proc || start >= end)
        return nullptr;

    if (!list_empty(&proc->vm_areas))
    {
        list_item_t* head = &proc->vm_areas;
        for (list_item_t* pos = head->next; pos != head; pos = pos->next)
        {
            // NOLINTNEXTLINE(clang-analyzer-security.ArrayBound)
            vm_area_t* existing = list_entry(pos, vm_area_t, list);
            if (!existing)
                continue;
            if (!(end <= existing->start || start >= existing->end)) // NOLINT(clang-analyzer-security.ArrayBound)
            {
                return nullptr; // overlap
            }
        }
    }

    vm_area_t* area = kmalloc(sizeof(vm_area_t));
    if (!area)
        return nullptr;

    area->start = start;
    area->end = end;
    area->flags = flags;
    list_add_tail(&area->list, &proc->vm_areas);
    proc->vm_area_count++;
    return area;
}

/**
 * Clone the virtual memory areas from one process to another.
 * @param dest Destination process
 * @param src Source process
 */
void vm_area_clone(process_t* dest, const process_t* src)
{
    if (!dest || !src)
        return;

    list_init_head(&dest->vm_areas);
    dest->vm_area_count = 0;

    vm_area_t* area;
    list_for_each_entry(area, &src->vm_areas, list)
    {
        vm_area_add(dest, area->start, area->end, area->flags);
    }
}

/**
 * Clear all virtual memory areas for a process.
 * @param proc Process to clear areas for
 */
void vm_area_clear(process_t* proc)
{
    if (!proc)
        return;

    vm_area_t *area, *tmp;
    list_for_each_entry_safe(area, tmp, &proc->vm_areas, list)
    {
        list_del(&area->list);
        kfree(area);
    }
    list_init_head(&proc->vm_areas);
    proc->vm_area_count = 0;
}

[[noreturn]] static void idle_task(void)
{
    while (1)
    {
        __asm__ volatile("hlt");
    }
}

/*
 * Create an idle thread for a CPU. Unlike regular threads, idle threads
 * are NOT added to the process thread list to avoid scheduler confusion.
 */
static thread_t* create_idle_thread(void)
{
    thread_t* thread = kmalloc(sizeof(thread_t));
    if (!thread)
        panic("Failed to allocate idle thread");
    memset(thread, 0, sizeof(thread_t));

    void* kstack = kmalloc(KSTACK_SIZE);
    if (!kstack)
    {
        kfree(thread);
        return nullptr;
    }
    memset(kstack, 0, KSTACK_SIZE);

    thread->tid = __atomic_fetch_add(&next_tid, 1, __ATOMIC_SEQ_CST);
    thread->process = kernel_process;
    thread->state = THREAD_READY;
    thread->is_idle = true;
    thread->is_user = false;
    thread->ticks_remaining = TIME_SLICE_TICKS;
    thread->kstack_top = (uint64_t)kstack + KSTACK_SIZE;

    init_fpu_state(&thread->fpu_state);

    uint64_t stack_ptr = thread->kstack_top - KSTACK_SYSCALL_HEADROOM;
    stack_ptr -= sizeof(struct context);
    struct context* ctx = (struct context*)stack_ptr;
    memset(ctx, 0, sizeof(struct context));

    ctx->rip = (uint64_t)thread_trampoline;
    ctx->r12 = (uint64_t)idle_task;
    thread->context = ctx;
    thread->rsp = stack_ptr;

    // Initialize list node but do NOT add to any list
    list_init_head(&thread->list);

    return thread;
}

/**
 * Create a scheduler thread for a CPU.
 * Each scheduler thread is responsible for managing the scheduling of threads
 * on a specific CPU. It runs at a higher priority than regular threads and
 * ensures fair and efficient thread execution.
 *
 * @param cpu_idx CPU index
 * @return Scheduler thread or nullptr on failure
 */
static thread_t* create_scheduler_thread(uint32_t cpu_idx)
{
    thread_t* thread = kmalloc(sizeof(thread_t));
    if (!thread)
        return nullptr;
    memset(thread, 0, sizeof(thread_t));

    void* kstack = kmalloc(KSTACK_SIZE);
    if (!kstack)
    {
        kfree(thread);
        return nullptr;
    }
    memset(kstack, 0, KSTACK_SIZE);

    thread->tid = -(1000 + (int)cpu_idx);
    thread->process = kernel_process;
    thread->state = THREAD_RUNNING;
    thread->is_idle = false;
    thread->is_user = false;
    thread->ticks_remaining = TIME_SLICE_TICKS;
    thread->kstack_top = (uint64_t)kstack + KSTACK_SIZE;

    init_fpu_state(&thread->fpu_state);

    uint64_t stack_ptr = thread->kstack_top - KSTACK_SYSCALL_HEADROOM;
    stack_ptr -= sizeof(struct context);
    // Align for direct C entry (scheduler_loop); SysV expects 16B alignment at call sites.
    stack_ptr &= ~0xFULL;
    struct context* ctx = (struct context*)stack_ptr;
    memset(ctx, 0, sizeof(struct context));
    ctx->rip = (uint64_t)scheduler_loop;

    thread->context = ctx;
    thread->rsp = stack_ptr;
    list_init_head(&thread->list);
    return thread;
}

static inline bool thread_state_valid_raw(uint32_t raw_state)
{
    return raw_state <= THREAD_TERMINATED;
}

static inline uint32_t thread_state_load_raw(const thread_t* t)
{
    return __atomic_load_n((const uint32_t*)&t->state, __ATOMIC_RELAXED);
    // __ATOMIC_RELAXED means no memory ordering constraints
}

static inline void thread_state_store(thread_t* t, thread_state_t state)
{
    __atomic_store_n((uint32_t*)&t->state, (uint32_t)state, __ATOMIC_RELAXED);
}

static bool process_in_list(const process_t* proc)
{
    if (!proc)
        return false;

    list_item_t* pos;
    list_for_each(pos, &process_list)
    {
        if (list_entry(pos, process_t, list) == proc)
            return true;
    }
    return false;
}

// ReSharper disable once CppDFAConstantParameter
static inline bool thread_is_ready(thread_t* t, bool allow_user, const char* ctx)
{
    uint32_t raw_state = thread_state_load_raw(t);
    if (!thread_state_valid_raw(raw_state))
    {
        boot_message(ERROR, "%s: invalid thread state pid=%d tid=%d state=%u", ctx, t->process ? t->process->pid : -1,
                     t->tid, raw_state);
        thread_state_store(t, THREAD_TERMINATED);
        return false;
    }

    process_t* proc = t->process;
    if (!proc || !process_in_list(proc))
    {
        boot_message(ERROR,
                     "%s: thread with stale process pid=%d tid=%d",
                     ctx,
                     proc ? proc->pid : -1,
                     t->tid);
        thread_state_store(t, THREAD_TERMINATED);
        return false;
    }

    thread_state_t state = (thread_state_t)raw_state;
    const bool userish = t->is_user || (t->saved_user_rsp != 0);
    // ReSharper disable once CppDFAUnreachableCode
    return state == THREAD_READY && !t->is_idle && (allow_user || !userish);
}

bool scheduler_tick(void)
{
    if (!scheduler_ready)
        return false;

    scheduler_ticks++;
    bool need_resched = false;

    spinlock_acquire(&scheduler_lock);
    process_t* p;
    list_for_each_entry(p, &process_list, list)
    {
        if (list_empty(&p->threads))
        {
            continue;
        }

        thread_t* t;
        list_for_each_entry(t, &p->threads, list)
        {
            uint32_t raw_state = thread_state_load_raw(t);
            if (!thread_state_valid_raw(raw_state))
            {
                boot_message(ERROR, "scheduler_tick: invalid thread state pid=%d tid=%d state=%u", p->pid, t->tid,
                             raw_state);
                thread_state_store(t, THREAD_TERMINATED);
                continue;
            }
        }
    }

    cpu_t* cpu = get_cpu();
    thread_t* curr = cpu != nullptr ? cpu->active_thread : nullptr;
    if (curr)
    {
        if (cpu && cpu->scheduler_thread == curr)
        {
            spinlock_release(&scheduler_lock);
            return need_resched;
        }

        const uintptr_t curr_addr = (uintptr_t)curr;
        const bool curr_aligned = (curr_addr % __alignof__(thread_t)) == 0;

        uint32_t raw_state = curr_aligned ? thread_state_load_raw(curr) : THREAD_TERMINATED;
        bool curr_valid = curr_aligned && thread_state_valid_raw(raw_state);
        if (!curr_valid)
        {
            const uintptr_t proc_addr = curr_aligned ? (uintptr_t)curr->process : 0;
            int pid = -1;
            int tid = -1;
            if (curr_aligned)
            {
                tid = curr->tid;
                if (curr->process && (proc_addr % __alignof__(process_t) == 0))
                    pid = curr->process->pid;
            }

            boot_message(ERROR,
                         "scheduler_tick: invalid current thread state raw=%u curr=0x%lx pid=%d tid=%d proc=0x%lx",
                         raw_state,
                         (unsigned long)curr_addr,
                         pid,
                         tid,
                         (unsigned long)proc_addr);

            raw_state = THREAD_READY;
            if (curr_aligned)
                thread_state_store(curr, THREAD_READY);
            need_resched = true; // Ask the scheduler to pick a safer thread.
            curr = nullptr;
        }

        if (curr)
        {
            thread_state_t curr_state = (thread_state_t)raw_state;
            if (curr->is_idle)
            {
                need_resched = true;
            }
            else if (curr_state == THREAD_RUNNING)
            {
                if (curr->ticks_remaining > 0)
                    curr->ticks_remaining--;

                if (curr->ticks_remaining == 0)
                {
                    thread_state_store(curr, THREAD_READY);
                    thread_list_move_to_tail(curr);
                    need_resched = true;
                }
            }
        }
    }

    spinlock_release(&scheduler_lock);
    return need_resched;
}

void process_init(void)
{
    spinlock_init(&scheduler_lock);

    // Initialize the first kernel process (initial kernel task)
    kernel_process = kmalloc(sizeof(process_t));
    if (!kernel_process)
    {
        boot_message(ERROR, "Process: Failed to allocate kernel process");
        return;
    }
    memset(kernel_process, 0, sizeof(process_t));
    kernel_process->pid = next_pid++;
    strcpy(kernel_process->name, "kernel");
    kernel_process->cwd[0] = '/';
    kernel_process->cwd[1] = '\0';
    vm_area_init(kernel_process);

    uint64_t cr3;
    __asm__ volatile("mov %0, cr3" : "=r"(cr3));
    kernel_process->pml4 = (pml4_t)cr3;

    thread_t* kernel_thread = kmalloc(sizeof(thread_t));
    if (!kernel_thread)
    {
        boot_message(ERROR, "Process: Failed to allocate kernel thread");
        return;
    }
    memset(kernel_thread, 0, sizeof(thread_t));

    kernel_thread->tid = next_tid++;
    kernel_thread->process = kernel_process;
    kernel_thread->state = THREAD_RUNNING;
    kernel_thread->ticks_remaining = TIME_SLICE_TICKS;
    kernel_thread->is_idle = false;

    // For the initial kernel thread, capture the current RSP and derive a stack window
    // so scheduler sanity checks consider it in-bounds. This thread is already running
    // on whatever bootstrap stack the BSP used, so align that RSP rather than an
    // arbitrary per-CPU kernel stack pointer.
    cpu_t* cpu = get_cpu();
    if (!cpu)
    {
        boot_message(ERROR, "Process: Failed to get current CPU for kernel thread");
        return;
    }
    uint64_t curr_rsp;
    __asm__ volatile("mov %0, rsp" : "=r"(curr_rsp));
    uint64_t aligned_base = curr_rsp & ~(uint64_t)(KSTACK_SIZE - 1);
    kernel_thread->kstack_top = aligned_base + KSTACK_SIZE;
    kernel_thread->rsp = curr_rsp;

    // If this CPU does not have a kernel stack pointer yet, seed it so syscalls have
    // something reasonable until threads switch away from the bootstrap stack.
    if (cpu && cpu->kernel_rsp == 0)
        cpu->kernel_rsp = kernel_thread->kstack_top;

    list_init_head(&kernel_process->threads);
    list_add_tail(&kernel_thread->list, &kernel_process->threads);

    list_add_tail(&kernel_process->list, &process_list);

    cpu->active_thread = kernel_thread;

    // Create per-CPU scheduler pseudo-threads (not in any runnable list)
    uint32_t cpu_count = smp_get_cpu_count();
    for (uint32_t i = 0; i < cpu_count && i < MAX_CPUS; i++)
    {
        thread_t* sched = create_scheduler_thread(i);
        cpu_t* c = smp_get_cpu_by_index(i);
        if (!sched || !c)
        {
            boot_message(ERROR, "Process: Failed to create scheduler thread for CPU %d", i);
            continue;
        }
        c->scheduler_thread = sched;
    }

    // Create idle threads for all CPUs
    // These are NOT added to the thread list - they're only used when no other thread is ready
    for (uint32_t i = 0; i < cpu_count && i < MAX_CPUS; i++)
    {
        idle_threads[i] = create_idle_thread();
        if (!idle_threads[i])
        {
            boot_message(ERROR, "Process: Failed to create idle thread for CPU %d", i);
        }
    }

    boot_message(INFO, "Process: Initialized kernel process PID %d with %d idle threads",
                 kernel_process->pid, cpu_count);
    scheduler_ready = true;

    smp_ap_scheduler_ready();
}

process_t* process_create(const char* name)
{
    process_t* proc = kmalloc(sizeof(process_t));
    if (!proc)
        return nullptr;
    memset(proc, 0, sizeof(process_t));
    vm_area_init(proc);

    spinlock_acquire(&scheduler_lock);
    proc->pid = next_pid++;
    spinlock_release(&scheduler_lock);

    strncpy(proc->name, name, PROCESS_NAME_MAX - 1);
    proc->name[PROCESS_NAME_MAX - 1] = '\0';

    process_t* current = get_current_process();
    if (current && current->cwd[0])
    {
        strncpy(proc->cwd, current->cwd, VFS_MAX_PATH - 1);
        proc->cwd[VFS_MAX_PATH - 1] = '\0';
    }
    else
    {
        proc->cwd[0] = '/';
        proc->cwd[1] = '\0';
    }

    uint64_t rflags;
    SPIN_LOCK_INT_SAVE(scheduler_lock, rflags);
    list_init_head(&proc->threads);
    list_add_tail(&proc->list, &process_list);
    SPIN_UNLOCK_INT_RESTORE(scheduler_lock, rflags);

    return proc;
}

void process_copy_fds(process_t* dest, const process_t* src)
{
    for (int i = 0; i < MAX_FDS; i++)
    {
        if (src->fd_table[i])
        {
            file_descriptor_t* old_desc = src->fd_table[i];
            file_descriptor_t* new_desc = kmalloc(sizeof(file_descriptor_t));
            if (new_desc)
            {
                memset(new_desc, 0, sizeof(file_descriptor_t));
                new_desc->flags = old_desc->flags;
                new_desc->offset = old_desc->offset;
                new_desc->ref = 1;

                if (old_desc->inode)
                {
                    // For pipes and special files, share the inode
                    // For regular files with a clone op, clone it
                    // Otherwise copy the inode
                    if (old_desc->inode->flags & VFS_PIPE)
                    {
                        // Pipes are shared across fork - just increment ref
                        new_desc->inode = old_desc->inode;
                        old_desc->inode->ref++;
                    }
                    else if (old_desc->inode->iops && old_desc->inode->iops->clone)
                    {
                        new_desc->inode = old_desc->inode->iops->clone(old_desc->inode);
                        if (new_desc->inode)
                            new_desc->inode->ref = 1;
                    }
                    else
                    {
                        new_desc->inode = kmalloc(sizeof(vfs_inode_t));
                        if (new_desc->inode)
                        {
                            memcpy(new_desc->inode, old_desc->inode, sizeof(vfs_inode_t));
                            new_desc->inode->ref = 1;
                        }
                    }
                }
                else
                {
                    kfree(new_desc);
                    dest->fd_table[i] = nullptr;
                    continue;
                }
                if (!new_desc->inode)
                {
                    kfree(new_desc);
                    dest->fd_table[i] = nullptr;
                    continue;
                }
                dest->fd_table[i] = new_desc;
            }
            else
            {
                dest->fd_table[i] = nullptr;
            }
        }
        else
        {
            dest->fd_table[i] = nullptr;
        }
    }
}

static void process_collect_threads_locked(const process_t* proc, list_item_t* free_list)
{
    thread_t *t, *next_t;
    list_for_each_entry_safe(t, next_t, &proc->threads, list)
    {
        if (!t)
            panic("%s: thread is null", __func__);

        list_del(&t->list);
        list_add_tail(&t->list, free_list);

        thread_state_store(t, THREAD_TERMINATED);
        t->process = nullptr;
        t->saved_user_rsp = 0;
    }
}

void process_destroy(process_t* proc)
{
    if (!proc)
        return;

    if (proc == kernel_process || (init_process && proc == init_process))
        return;

    for (;;)
    {
        uint64_t rflags;
        SPIN_LOCK_INT_SAVE(scheduler_lock, rflags);

        thread_t* t;
        list_for_each_entry(t, &proc->threads, list)
        {
            thread_state_store(t, THREAD_TERMINATED);
        }

        const bool can_reap = process_can_reap_locked(proc);
        SPIN_UNLOCK_INT_RESTORE(scheduler_lock, rflags);

        if (can_reap)
            break;

        yield();
    }

    process_destroy_now(proc);
}

void process_reap(process_t* proc)
{
    if (!proc)
        return;
    process_destroy_now(proc);
}

static void process_destroy_now(process_t* proc)
{
    list_item_t free_list = LIST_HEAD_INIT(free_list);
    uint64_t rflags;
    SPIN_LOCK_INT_SAVE(scheduler_lock, rflags);

    // Re-verify that no thread is active on any CPU before destroying.
    // The state could have changed between process_can_reap_locked() and now.
    if (!process_can_reap_locked(proc))
    {
        SPIN_UNLOCK_INT_RESTORE(scheduler_lock, rflags);
        return;
    }

    if (process_in_list(proc))
        list_del(&proc->list);

    process_collect_threads_locked(proc, &free_list);

    SPIN_UNLOCK_INT_RESTORE(scheduler_lock, rflags);

    thread_t *t, *next_t;
    list_for_each_entry_safe(t, next_t, &free_list, list)
    {
        if (!t)
            panic("%s: thread is null", __func__);

        list_del(&t->list);
        auto kernel_stack_base = (void*)(t->kstack_top - KSTACK_SIZE);
        kfree(kernel_stack_base);
        kfree(t);
    }

    // Free file descriptors (respecting reference counts)
    // Note: Multiple fd entries can point to the same descriptor due to dup()
    for (int i = 0; i < MAX_FDS; i++)
    {
        if (proc->fd_table[i])
        {
            file_descriptor_t* desc = proc->fd_table[i];

            // Clear all fd table entries pointing to this descriptor first
            // This prevents double-processing the same descriptor
            for (int j = i; j < MAX_FDS; j++)
            {
                if (proc->fd_table[j] == desc)
                {
                    proc->fd_table[j] = nullptr;
                    if (j > i)
                    {
                        // Another fd points to the same descriptor, decrement ref
                        if (desc->ref > 0)
                            desc->ref--;
                    }
                }
            }

            // Now handle this descriptor's cleanup
            // Decrement file descriptor ref count (for cross-process sharing)
            if (desc->ref > 1)
            {
                desc->ref--;
                continue; // Other processes still reference this descriptor
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
        }
    }

    vm_area_clear(proc);

    // Free address space
    if (proc->pml4 && proc->pid != 1)
    {
        vmm_destroy_pml4(proc->pml4);
    }

    kfree(proc);
}

thread_t* thread_create(process_t* process, void (*entry)(void), bool is_user)
{
    thread_t* thread = kmalloc(sizeof(thread_t));
    if (!thread)
        return nullptr;
    memset(thread, 0, sizeof(thread_t));

    spinlock_acquire(&scheduler_lock);
    thread->tid = next_tid++;
    spinlock_release(&scheduler_lock);

    thread->process = process;
    thread_state_store(thread, THREAD_READY);
    thread->is_user = is_user;
    thread->ticks_remaining = TIME_SLICE_TICKS;

    init_fpu_state(&thread->fpu_state);

    void* stack = kmalloc(KSTACK_SIZE);
    if (!stack)
    {
        kfree(thread);
        return nullptr;
    }
    thread->kstack_top = (uint64_t)stack + KSTACK_SIZE;

    // Reserve the very top of the stack for syscall entry pushes so they don't
    // clobber the context-switch frame we place near the top.
    uint64_t* stack_ptr = (uint64_t*)(thread->kstack_top - KSTACK_SYSCALL_HEADROOM);

    // Reserve space for context
    stack_ptr -= sizeof(struct context) / sizeof(uint64_t);
    struct context* ctx = (struct context*)stack_ptr;

    memset(ctx, 0, sizeof(struct context));
    ctx->rip = (uint64_t)thread_trampoline;
    ctx->r12 = (uint64_t)entry; // R12 holds entry point

    thread->context = ctx;
    thread->rsp = (uint64_t)ctx;

    uint64_t rflags;
    SPIN_LOCK_INT_SAVE(scheduler_lock, rflags);
    list_add_tail(&thread->list, &process->threads);
    SPIN_UNLOCK_INT_RESTORE(scheduler_lock, rflags);

    return thread;
}

void smp_init_ap_scheduler(void)
{
    // Set this CPU's scheduler thread as active
    cpu_t* cpu = get_cpu();
    uint32_t cpu_idx = (uint32_t)cpu->cpu_index;

    if (cpu_idx < MAX_CPUS && cpu->scheduler_thread)
    {
        thread_t* schedt = cpu->scheduler_thread;
        cpu->active_thread = schedt;
        schedt->state = THREAD_RUNNING;

        // Ensure the syscall / TSS stack uses the scheduler stack for this CPU.
        cpu->kernel_rsp = schedt->kstack_top;
        tss_set_stack(cpu->kernel_rsp);

        // The AP enters `ap_main` on a Limine-provided bootstrap stack, not on the
        // per-thread kernel stack. If we don't switch stacks here, the first time
        // this CPU gets preempted, we will save an out-of-range RSP into the scheduler
        // thread, and later scheduler validation will reject that thread.
        //
        // Switch onto the scheduler thread stack by performing a one-way context switch
        // from a synthetic "bootstrap" thread frame. We do NOT hold scheduler_lock here
        // because scheduler_loop() will acquire it at the start of each iteration.
        __asm__ volatile("cli");
        thread_t bootstrap = {};
        bootstrap.tid = -1;
        bootstrap.process = kernel_process;
        bootstrap.state = THREAD_RUNNING;
        switch_to(&bootstrap, schedt);
        __builtin_unreachable();
    }
}

thread_t* get_current_thread(void)
{
    cpu_t* cpu = get_cpu();
    if (!cpu)
        return nullptr;
    return cpu->active_thread;
}

process_t* get_current_process(void)
{
    thread_t* t = get_current_thread();
    if (t)
        return t->process;
    return nullptr;
}

/**
 * Check if a thread is currently the active thread on any CPU.
 * @warning Caller must hold scheduler_lock.
 * @param t Thread to check
 */
static bool thread_is_active_on_any_cpu(thread_t* t)
{
    if (!t)
        return false;

    const uint32_t cpu_count = smp_get_cpu_count();
    for (uint32_t i = 0; i < cpu_count; i++)
    {
        cpu_t* c = smp_get_cpu_by_index(i);
        if (c && c->active_thread == t)
            return true;
    }
    return false;
}

bool process_can_reap_locked(process_t* proc)
{
    if (!proc)
        return false;

    thread_t* t;
    list_for_each_entry(t, &proc->threads, list)
    {
        if (thread_is_active_on_any_cpu(t))
            return false;

        uint32_t raw_state = thread_state_load_raw(t);
        if (!thread_state_valid_raw(raw_state))
        {
            thread_state_store(t, THREAD_TERMINATED);
            return false;
        }
        if ((thread_state_t)raw_state != THREAD_TERMINATED)
            return false;
    }

    return true;
}

/**
 * Scan all processes and return the first runnable thread.
 * @warning Caller must hold scheduler_lock.
 * @param allow_user Whether to consider user threads
 */
// ReSharper disable once CppDFAConstantParameter
static thread_t* find_any_runnable_thread(const bool allow_user)
{
    process_t* p;
    list_for_each_entry(p, &process_list, list)
    {
        thread_t* t;
        list_for_each_entry(t, &p->threads, list)
        {
            if (thread_is_ready(t, allow_user, "sched") && !thread_is_active_on_any_cpu(t))
                return t;
        }
    }
    return nullptr;
}

// Round-robin variant to avoid starving later-created processes (important for tests).
static process_t* rr_last_proc[MAX_CPUS] = {nullptr};

/**
 * Find any runnable thread using round-robin across processes.
 * @warning Caller must hold scheduler_lock.
 * @param cpu CPU to consider (or nullptr for any)
 * @param allow_user Whether to consider user threads
 * @return Runnable thread or nullptr if none found
 */
// ReSharper disable once CppDFAConstantParameter
static thread_t* find_any_runnable_thread_rr(cpu_t* cpu, const bool allow_user)
{
    if (!cpu)
        return find_any_runnable_thread(allow_user);

    const int cpu_idx = cpu->cpu_index;
    if (cpu_idx < 0 || cpu_idx >= (int)MAX_CPUS)
        return find_any_runnable_thread(allow_user);

    list_item_t* head = &process_list;
    process_t* startp = rr_last_proc[cpu_idx];
    list_item_t* start = (startp != nullptr && process_in_list(startp)) ? startp->list.next : head->next;
    if (start == head)
        start = head->next;

    for (list_item_t* pos = start; pos != head; pos = pos->next)
    {
        process_t* p = list_entry(pos, process_t, list);
        thread_t* t;
        list_for_each_entry(t, &p->threads, list)
        {
            if (thread_is_ready(t, allow_user, "sched") && !thread_is_active_on_any_cpu(t))
            {
                rr_last_proc[cpu_idx] = p;
                return t;
            }
        }
    }

    // Wrap-around: head -> start
    for (list_item_t* pos = head->next; pos != start && pos != head; pos = pos->next)
    {
        process_t* p = list_entry(pos, process_t, list);
        thread_t* t;
        list_for_each_entry(t, &p->threads, list)
        {
            if (thread_is_ready(t, allow_user, "sched") && !thread_is_active_on_any_cpu(t))
            {
                rr_last_proc[cpu_idx] = p;
                return t;
            }
        }
    }

    return nullptr;
}

/**
 * xv6-style scheduler loop running on a per-CPU scheduler pseudo-thread stack.
 * @note we do NOT keep scheduler_lock held while running normal threads.
 */
[[noreturn]] static void scheduler_loop(void)
{
    cpu_t* cpu = get_cpu();
    if (!cpu)
        hcf();

    thread_t* schedt = cpu->scheduler_thread;
    if (!schedt)
        hcf();

    cpu->active_thread = schedt;
    cpu->user_rsp = 0;

    for (;;)
    {
        spinlock_acquire(&scheduler_lock);
        constexpr bool allow_user = true;
        thread_t* next = find_any_runnable_thread_rr(cpu, allow_user);
        if (!next)
        {
            thread_t* idle = nullptr;
            const int cpu_idx = cpu->cpu_index;
            if (cpu_idx >= 0 && cpu_idx < (int)MAX_CPUS)
                idle = idle_threads[cpu_idx];
            if (!idle)
                idle = idle_threads[0];
            if (!idle)
            {
                spinlock_release(&scheduler_lock);
                __asm__ volatile("sti; hlt; cli");
                continue;
            }
            next = idle;
        }

        const uintptr_t ktop = next->kstack_top;
        const uintptr_t kbase = (ktop != 0) ? (ktop - KSTACK_SIZE) : 0;
        if (ktop == 0 || next->rsp < kbase || next->rsp >= ktop)
        {
            boot_message(ERROR,
                         "scheduler_loop: invalid rsp pid=%d tid=%d rsp=0x%lx kstack=[0x%lx-0x%lx)",
                         next->process ? next->process->pid : -1,
                         next->tid,
                         next->rsp,
                         kbase,
                         ktop);
            thread_state_store(next, THREAD_TERMINATED);
            spinlock_release(&scheduler_lock);
            continue;
        }
        const uintptr_t rip_slot = next->rsp + (6 * sizeof(uint64_t));
        if (rip_slot < kbase || rip_slot + sizeof(uint64_t) > ktop)
        {
            boot_message(ERROR,
                         "scheduler_loop: invalid rip slot pid=%d tid=%d rsp=0x%lx kstack=[0x%lx-0x%lx)",
                         next->process ? next->process->pid : -1,
                         next->tid,
                         next->rsp,
                         kbase,
                         ktop);
            thread_state_store(next, THREAD_TERMINATED);
            spinlock_release(&scheduler_lock);
            continue;
        }
        const uint64_t saved_rip = *(const uint64_t*)rip_slot;
        const uintptr_t user_top = g_hhdm_offset ? g_hhdm_offset : 0x0000800000000000ull;
        if (saved_rip == 0 || saved_rip < user_top)
        {
            boot_message(ERROR,
                         "scheduler_loop: bad rip pid=%d tid=%d rip=0x%lx rsp=0x%lx",
                         next->process ? next->process->pid : -1,
                         next->tid,
                         saved_rip,
                         next->rsp);
            thread_state_store(next, THREAD_TERMINATED);
            spinlock_release(&scheduler_lock);
            continue;
        }

        if (next->process && next->process->pml4)
            vmm_switch_pml4(next->process->pml4);

        syscall_set_stack(next->kstack_top);

        cpu->user_rsp = next->saved_user_rsp;
        restore_fpu_state(&next->fpu_state);

        cpu->active_thread = next;
        next->state = THREAD_RUNNING;
        next->ticks_remaining = TIME_SLICE_TICKS;

        spinlock_release(&scheduler_lock);
        switch_to(schedt, next);

        vmm_switch_pml4(kernel_process->pml4);
        cpu->active_thread = schedt;
        cpu->user_rsp = 0;
    }
}

void schedule(void)
{
    // Save interrupt state and disable interrupts
    uint64_t rflags;
    __asm__ volatile("pushfq; pop %0; cli" : "=r"(rflags));

    cpu_t* cpu = get_cpu();
    thread_t* curr = cpu ? cpu->active_thread : nullptr;
    thread_t* schedt = cpu ? cpu->scheduler_thread : nullptr;
    if (!curr || !schedt)
    {
        if (rflags & RFLAGS_IF)
            __asm__ volatile("sti");
        return;
    }

    spinlock_acquire(&scheduler_lock);

    // If we are preempting a running thread (e.g., from timer interrupt),
    // mark it runnable so the scheduler can pick it again.
    if (curr != schedt && curr->state == THREAD_RUNNING && !curr->is_idle)
    {
        thread_state_store(curr, THREAD_READY);
        thread_list_move_to_tail(curr);
    }

    // Preserve per-thread user rsp scratch and FPU state before switching out.
    curr->saved_user_rsp = cpu ? cpu->user_rsp : 0;
    save_fpu_state(&curr->fpu_state);

    spinlock_release(&scheduler_lock);
    switch_to(curr, schedt);

    if (rflags & RFLAGS_IF)
        __asm__ volatile("sti");
}

void thread_sleep(void* chan, spinlock_t* lock)
{
    thread_t* curr = get_current_thread();
    if (!curr)
        return;

    // Save interrupt state and disable interrupts to avoid deadlock with scheduler_lock
    uint64_t rflags;
    __asm__ volatile("pushfq; pop %0; cli" : "=r"(rflags));

    // Acquire scheduler lock for state transition; release any provided lock.
    const bool caller_had_scheduler_lock = (lock == &scheduler_lock);
    if (!caller_had_scheduler_lock)
    {
        spinlock_acquire(&scheduler_lock);
        if (lock)
            spinlock_release(lock);
    }

    curr->chan = chan;
    curr->state = THREAD_BLOCKED;

    // Release scheduler lock so other threads can run while we sleep.
    spinlock_release(&scheduler_lock);

    schedule();

    curr->chan = nullptr;

    // Reacquire locks to restore caller expectations.
    if (!caller_had_scheduler_lock)
    {
        if (lock)
            spinlock_acquire(lock);
    }
    else
    {
        spinlock_acquire(&scheduler_lock);
    }

    if (rflags & RFLAGS_IF)
        __asm__ volatile("sti");
}

void thread_wakeup(void* chan)
{
    uint64_t rflags;
    SPIN_LOCK_INT_SAVE(scheduler_lock, rflags);
    process_t* p;
    list_for_each_entry(p, &process_list, list)
    {
        thread_t* t;
        list_for_each_entry(t, &p->threads, list)
        {
            uint32_t raw_state = thread_state_load_raw(t);
            if (!thread_state_valid_raw(raw_state))
            {
                boot_message(ERROR, "thread_wakeup: invalid thread state pid=%d tid=%d state=%u", p->pid, t->tid,
                             raw_state);
                thread_state_store(t, THREAD_TERMINATED);
                continue;
            }

            thread_state_t state = (thread_state_t)raw_state;
            if (state == THREAD_BLOCKED && t->chan == chan)
            {
                thread_state_store(t, THREAD_READY);
                t->chan = nullptr;
            }
        }
    }
    SPIN_UNLOCK_INT_RESTORE(scheduler_lock, rflags);
}

void yield(void)
{
    schedule();
}

static const char* thread_state_str(thread_state_t state)
{
    switch (state)
    {
    case THREAD_READY:
        return "READY";
    case THREAD_RUNNING:
        return "RUN";
    case THREAD_BLOCKED:
        return "SLEEP";
    case THREAD_TERMINATED:
        return "DEAD";
    default:
        return "?";
    }
}

void process_dump(void)
{
    uint64_t rflags;
    SPIN_LOCK_INT_SAVE(scheduler_lock, rflags);
    printk("\n%-5s %-5s %-6s %s\n", "PID", "TID", "STATE", "NAME");
    process_t* p;
    list_for_each_entry(p, &process_list, list)
    {
        thread_t* t;
        list_for_each_entry(t, &p->threads, list)
        {
            uint32_t raw_state = thread_state_load_raw(t);
            const char* state_str = "BAD";

            if (thread_state_valid_raw(raw_state))
            {
                state_str = thread_state_str((thread_state_t)raw_state);
            }

            printk("%-5d %-5d %-6s %s\n",
                   p->pid,
                   t->tid,
                   state_str,
                   p->name);
        }
    }
    SPIN_UNLOCK_INT_RESTORE(scheduler_lock, rflags);
}
