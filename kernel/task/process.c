#include "process.h"
#include "heap.h"
#include "string.h"
#include "terminal.h"
#include "cpu.h"
#include "vmm.h"
#include "syscall.h"
#include "spinlock.h"
#include "apic.h"

#define TIME_SLICE_TICKS ((TIME_SLICE_MS * TIMER_FREQUENCY_HZ) / 1000)

list_head_t process_list __attribute__((aligned(16))) = LIST_HEAD_INIT(process_list);
process_t* kernel_process = nullptr;
thread_t* idle_thread = nullptr;
static int next_pid = 1;
static int next_tid = 1;
volatile uint64_t scheduler_ticks = 0;

spinlock_t scheduler_lock;
static bool scheduler_ready = false; // Ignore timer ticks until process_init completes

extern void fork_return(void);

void vm_area_init(process_t* proc)
{
    if (!proc)
        return;
    INIT_LIST_HEAD(&proc->vm_areas);
    proc->vm_area_count = 0;
}

vm_area_t* vm_area_add(process_t* proc, uint64_t start, uint64_t end, uint32_t flags)
{
    if (!proc || start >= end)
        return nullptr;

    if (!list_empty(&proc->vm_areas))
    {
        list_head_t* head = &proc->vm_areas;
        for (list_head_t* pos = head->next; pos != head; pos = pos->next)
        {
            if (pos == head)
                break;
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

void vm_area_clone(process_t* dest, const process_t* src)
{
    if (!dest || !src)
        return;

    INIT_LIST_HEAD(&dest->vm_areas);
    dest->vm_area_count = 0;

    vm_area_t* area;
    list_for_each_entry(area, &src->vm_areas, list)
    {
        vm_area_add(dest, area->start, area->end, area->flags);
    }
}

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
    INIT_LIST_HEAD(&proc->vm_areas);
    proc->vm_area_count = 0;
}

[[noreturn]] static void idle_task(void)
{
    while (1)
    {
        __asm__ volatile("hlt");
    }
}

#ifdef TEST_MODE
// #define SCHED_LOG(fmt, ...) printf(fmt "\n", ##__VA_ARGS__)
#define SCHED_LOG(fmt, ...) ((void)0)
#else
#define SCHED_LOG(fmt, ...) ((void)0)
#endif

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
            if (t->state == THREAD_BLOCKED && t->sleep_until && t->sleep_until <= scheduler_ticks)
            {
                t->state = THREAD_READY;
                t->sleep_until = 0;
                need_resched = true;
            }
        }
    }

    thread_t* curr = get_current_thread();
    if (curr)
    {
        if (curr->is_idle)
        {
            need_resched = true;
        }
        else if (curr->state == THREAD_RUNNING)
        {
            if (curr->ticks_remaining > 0)
                curr->ticks_remaining--;

            if (curr->ticks_remaining == 0)
                need_resched = true;
        }
    }

    spinlock_release(&scheduler_lock);
    return need_resched;
}

void process_init(void)
{
    spinlock_init(&scheduler_lock);

    // Initialize the first kernel process (idle task / initial kernel task)
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

    // Use current CR3
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

    // For the initial kernel thread, we assume we are running on a valid stack.
    cpu_t* cpu = get_cpu();
    kernel_thread->kstack_top = cpu->kernel_rsp;

    kernel_thread->is_idle = false;

    INIT_LIST_HEAD(&kernel_process->threads);
    list_add_tail(&kernel_thread->list, &kernel_process->threads);

    list_add_tail(&kernel_process->list, &process_list);

    // Set current thread for this CPU
    cpu->active_thread = kernel_thread;

    // Create the actual idle thread
    idle_thread = thread_create(kernel_process, idle_task, false);
    if (idle_thread)
    {
        idle_thread->is_idle = true;
    }
    else
    {
        boot_message(ERROR, "Process: Failed to create idle thread");
    }

    boot_message(INFO, "Process: Initialized kernel process PID %d", kernel_process->pid);
    scheduler_ready = true;
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
    __asm__ volatile("pushfq; pop %0; cli" : "=r"(rflags));
    spinlock_acquire(&scheduler_lock);
    INIT_LIST_HEAD(&proc->threads);
    list_add_tail(&proc->list, &process_list);
    spinlock_release(&scheduler_lock);
    if (rflags & RFLAGS_IF)
        __asm__ volatile("sti");

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
                new_desc->flags = old_desc->flags;
                new_desc->offset = old_desc->offset;
                if (old_desc->inode)
                {
                    if (old_desc->inode->iops && old_desc->inode->iops->clone)
                    {
                        new_desc->inode = old_desc->inode->iops->clone(old_desc->inode);
                    }
                    else
                    {
                        new_desc->inode = kmalloc(sizeof(vfs_inode_t));
                        if (new_desc->inode)
                        {
                            memcpy(new_desc->inode, old_desc->inode, sizeof(vfs_inode_t));
                        }
                    }
                }
                else
                {
                    new_desc->inode = nullptr;
                }
                dest->fd_table[i] = new_desc;
            }
        }
        else
        {
            dest->fd_table[i] = nullptr;
        }
    }
}

void process_destroy(process_t* proc)
{
    if (!proc)
        return;

    // Free threads
    thread_t *t, *next_t;
    list_for_each_entry_safe(t, next_t, &proc->threads, list)
    {
        list_del(&t->list);

        // Free kernel stack
        // Stack size is hardcoded 16384 in thread_create
        void* stack_base = (void*)(t->kstack_top - 16384);
        kfree(stack_base);

        kfree(t);
    }

    // Free file descriptors
    for (int i = 0; i < MAX_FDS; i++)
    {
        if (proc->fd_table[i])
        {
            file_descriptor_t* desc = proc->fd_table[i];
            if (desc->inode)
            {
                vfs_close(desc->inode);
                if (desc->inode != vfs_root)
                {
                    kfree(desc->inode);
                }
            }
            kfree(desc);
            proc->fd_table[i] = nullptr;
        }
    }

    // Free vm areas
    vm_area_clear(proc);

    // Free address space
    if (proc->pml4 && proc->pid != 1)
    {
        vmm_destroy_pml4(proc->pml4);
    }

    uint64_t rflags;
    __asm__ volatile("pushfq; pop %0; cli" : "=r"(rflags));
    spinlock_acquire(&scheduler_lock);
    list_del(&proc->list);
    spinlock_release(&scheduler_lock);
    if (rflags & RFLAGS_IF)
        __asm__ volatile("sti");

    kfree(proc);
}

thread_t* thread_create(process_t* process, void (*entry)(void), [[maybe_unused]] bool is_user)
{
    thread_t* thread = kmalloc(sizeof(thread_t));
    if (!thread)
        return nullptr;
    memset(thread, 0, sizeof(thread_t));

    spinlock_acquire(&scheduler_lock);
    thread->tid = next_tid++;
    spinlock_release(&scheduler_lock);

    thread->process = process;
    thread->state = THREAD_READY;
    thread->ticks_remaining = TIME_SLICE_TICKS;

    init_fpu_state(&thread->fpu_state);

    void* stack = kmalloc(16384); // 16KB stack
    if (!stack)
    {
        kfree(thread);
        return nullptr;
    }
    thread->kstack_top = (uint64_t)stack + 16384;

    uint64_t* stack_ptr = (uint64_t*)thread->kstack_top;

    extern void thread_trampoline(void);

    // Reserve space for context
    stack_ptr -= sizeof(struct context) / sizeof(uint64_t);
    struct context* ctx = (struct context*)stack_ptr;

    memset(ctx, 0, sizeof(struct context));
    ctx->rip = (uint64_t)thread_trampoline;
    ctx->r12 = (uint64_t)entry; // R12 holds entry point

    thread->context = ctx;

    uint64_t rflags;
    __asm__ volatile("pushfq; pop %0; cli" : "=r"(rflags));
    spinlock_acquire(&scheduler_lock);
    list_add_tail(&thread->list, &process->threads);
    spinlock_release(&scheduler_lock);
    if (rflags & RFLAGS_IF)
        __asm__ volatile("sti");

    return thread;
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

// Internal scheduler function. Assumes scheduler_lock is held.
static void sched(void)
{
    cpu_t* cpu = get_cpu();
    thread_t* curr = cpu->active_thread;

#ifdef TEST_MODE
    if (curr)
    {
        SCHED_LOG("schedule enter PID %d TID %d state=%d",
                  curr->process ? curr->process->pid : -1,
                  curr->tid,
                  curr->state);
    }
#endif

    thread_t* next_thread = nullptr;
    if (!curr)
        return;

    process_t* p = curr->process;

    // Search remaining threads in current process
    list_head_t* t_node = curr->list.next;
    while (t_node != &p->threads)
    {
        thread_t* t = list_entry(t_node, thread_t, list);
        if (t->state == THREAD_READY && !t->is_idle)
        {
            next_thread = t;
            goto found;
        }
        t_node = t_node->next;
    }

    // Search subsequent processes
    list_head_t* p_node = p->list.next;
    while (p_node != &process_list)
    {
        process_t* next_p = list_entry(p_node, process_t, list);
        thread_t* t;
        list_for_each_entry(t, &next_p->threads, list)
        {
            if (t->state == THREAD_READY && !t->is_idle)
            {
                next_thread = t;
                goto found;
            }
        }
        p_node = p_node->next;
    }

    // Search from beginning of process list to current process
    p_node = process_list.next;
    while (p_node != &p->list)
    {
        process_t* prev_p = list_entry(p_node, process_t, list);
        thread_t* t;
        list_for_each_entry(t, &prev_p->threads, list)
        {
            if (t->state == THREAD_READY && !t->is_idle)
            {
                next_thread = t;
                goto found;
            }
        }
        p_node = p_node->next;
    }

    // Search current process from start to current thread
    t_node = p->threads.next;
    while (t_node != &curr->list)
    {
        thread_t* t = list_entry(t_node, thread_t, list);
        if (t->state == THREAD_READY && !t->is_idle)
        {
            next_thread = t;
            goto found;
        }
        t_node = t_node->next;
    }

    if (!next_thread)
    {
        next_thread = idle_thread;
    }

found:
    if (next_thread && next_thread != curr)
    {
        thread_t* prev = curr;

        // Switch address space if processes are different
        if (prev->process != next_thread->process)
        {
            if (next_thread->process->pml4)
            {
                vmm_switch_pml4(next_thread->process->pml4);
            }
        }

        // Update syscall stack for the next thread
        syscall_set_stack(next_thread->kstack_top);

        // Save and restore user_rsp_scratch for syscalls
        prev->saved_user_rsp = cpu->user_rsp;
        cpu->user_rsp = next_thread->saved_user_rsp;

        save_fpu_state(&prev->fpu_state);
        restore_fpu_state(&next_thread->fpu_state);

        cpu->active_thread = next_thread;
        next_thread->state = THREAD_RUNNING;
        next_thread->ticks_remaining = TIME_SLICE_TICKS;
        if (prev->state == THREAD_RUNNING)
            prev->state = THREAD_READY;

        SCHED_LOG("switch PID %d TID %d (state=%d) -> PID %d TID %d (state=%d)",
                  prev->process ? prev->process->pid : -1,
                  prev->tid,
                  prev->state,
                  next_thread->process ? next_thread->process->pid : -1,
                  next_thread->tid,
                  next_thread->state);

        switch_to(prev, next_thread);

        // Lock is released by the new thread (either thread_trampoline or here)
        // spinlock_release(&scheduler_lock); // Handled by caller
    }
    else
    {
        SCHED_LOG("no switch, staying on PID %d TID %d (state=%d)",
                  curr->process ? curr->process->pid : -1, curr->tid, curr->state);
        // spinlock_release(&scheduler_lock); // Handled by caller
    }
}

void schedule(void)
{
    // Save interrupt state and disable interrupts
    uint64_t rflags;
    __asm__ volatile("pushfq; pop %0; cli" : "=r"(rflags));

    thread_t* curr = get_current_thread();
    if (!curr)
    {
        if (rflags & RFLAGS_IF)
            __asm__ volatile("sti");
        return;
    }

    spinlock_acquire(&scheduler_lock);
    sched();
    spinlock_release(&scheduler_lock);

    // Restore interrupt state
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

    // Must acquire scheduler_lock to change state and sleep atomically
    if (lock != &scheduler_lock)
    {
        spinlock_acquire(&scheduler_lock);
        if (lock)
            spinlock_release(lock);
    }

    curr->chan = chan;
    curr->state = THREAD_BLOCKED;

    sched();

    curr->chan = nullptr;

    if (lock != &scheduler_lock)
    {
        spinlock_release(&scheduler_lock);
        if (lock)
            spinlock_acquire(lock);
    }

    // Restore interrupt state
    if (rflags & RFLAGS_IF)
        __asm__ volatile("sti");
}

void thread_wakeup(void* chan)
{
    // Save interrupt state and disable interrupts
    uint64_t rflags;
    __asm__ volatile("pushfq; pop %0; cli" : "=r"(rflags));

    spinlock_acquire(&scheduler_lock);
    process_t* p;
    list_for_each_entry(p, &process_list, list)
    {
        thread_t* t;
        list_for_each_entry(t, &p->threads, list)
        {
            if (t->state == THREAD_BLOCKED && t->chan == chan)
            {
                t->state = THREAD_READY;
                t->chan = nullptr;
            }
        }
    }
    spinlock_release(&scheduler_lock);

    // Restore interrupt state
    if (rflags & RFLAGS_IF)
        __asm__ volatile("sti");
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
    __asm__ volatile("pushfq; pop %0; cli" : "=r"(rflags));

    spinlock_acquire(&scheduler_lock);
    printk("\n%-5s %-5s %-6s %s\n", "PID", "TID", "STATE", "NAME");
    process_t* p;
    list_for_each_entry(p, &process_list, list)
    {
        thread_t* t;
        list_for_each_entry(t, &p->threads, list)
        {
            printk("%-5d %-5d %-6s %s%s\n",
                   p->pid,
                   t->tid,
                   thread_state_str(t->state),
                   p->name,
                   t->is_idle ? " (idle)" : "");
        }
    }
    spinlock_release(&scheduler_lock);

    if (rflags & RFLAGS_IF)
        __asm__ volatile("sti");
}
