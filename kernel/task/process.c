#include "process.h"
#include "heap.h"
#include "string.h"
#include "terminal.h"
#include "cpu.h"
#include "vmm.h"
#include "syscall.h"
#include "spinlock.h"

process_t *process_list = NULL;
static int next_pid = 1;
static int next_tid = 1;
volatile uint64_t scheduler_ticks = 0;

spinlock_t scheduler_lock;

void scheduler_tick(void)
{
    scheduler_ticks++;

    spinlock_acquire(&scheduler_lock);
    process_t *p = process_list;
    while (p)
    {
        thread_t *t = p->threads;
        while (t)
        {
            if (t->state == THREAD_BLOCKED && t->sleep_until && t->sleep_until <= scheduler_ticks)
            {
                t->state = THREAD_READY;
                t->sleep_until = 0;
            }
            t = t->next;
        }
        p = p->next;
    }
    spinlock_release(&scheduler_lock);
}

void process_init(void)
{
    spinlock_init(&scheduler_lock);

    // Initialize the first kernel process (idle task / initial kernel task)
    process_t *kernel_process = kmalloc(sizeof(process_t));
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

    // Use current CR3
    uint64_t cr3;
    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
    kernel_process->pml4 = (pml4_t)cr3;

    thread_t *kernel_thread = kmalloc(sizeof(thread_t));
    if (!kernel_thread)
    {
        boot_message(ERROR, "Process: Failed to allocate kernel thread");
        return;
    }
    memset(kernel_thread, 0, sizeof(thread_t));

    kernel_thread->tid = next_tid++;
    kernel_thread->process = kernel_process;
    kernel_thread->state = THREAD_RUNNING;

    // For the initial kernel thread, we assume we are running on a valid stack.
    cpu_t *cpu = get_cpu();
    kernel_thread->kstack_top = cpu->kernel_rsp;

    kernel_process->threads = kernel_thread;
    process_list = kernel_process;
    
    // Set current thread for this CPU
    cpu->active_thread = kernel_thread;

    boot_message(INFO, "Process: Initialized kernel process PID %d", kernel_process->pid);
}

process_t *process_create(const char *name)
{
    process_t *proc = kmalloc(sizeof(process_t));
    if (!proc)
        return NULL;
    memset(proc, 0, sizeof(process_t));

    spinlock_acquire(&scheduler_lock);
    proc->pid = next_pid++;
    spinlock_release(&scheduler_lock);

    strncpy(proc->name, name, PROCESS_NAME_MAX - 1);
    
    process_t *current = get_current_process();
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

    spinlock_acquire(&scheduler_lock);
    proc->next = process_list;
    process_list = proc;
    spinlock_release(&scheduler_lock);

    return proc;
}

thread_t *thread_create(process_t *process, void (*entry)(void), [[maybe_unused]] bool is_user)
{
    thread_t *thread = kmalloc(sizeof(thread_t));
    if (!thread)
        return NULL;
    memset(thread, 0, sizeof(thread_t));

    spinlock_acquire(&scheduler_lock);
    thread->tid = next_tid++;
    spinlock_release(&scheduler_lock);

    thread->process = process;
    thread->state = THREAD_READY;

    // Initialize FPU state
    init_fpu_state(&thread->fpu_state);

    // Allocate kernel stack
    void *stack = kmalloc(16384); // 16KB stack
    if (!stack)
    {
        kfree(thread);
        return NULL;
    }
    thread->kstack_top = (uint64_t)stack + 16384;

    uint64_t *stack_ptr = (uint64_t *)thread->kstack_top;

    extern void thread_trampoline(void);

    // Reserve space for context
    stack_ptr -= sizeof(struct context) / sizeof(uint64_t);
    struct context *ctx = (struct context *)stack_ptr;

    memset(ctx, 0, sizeof(struct context));
    ctx->rip = (uint64_t)thread_trampoline;
    ctx->r12 = (uint64_t)entry; // R12 holds entry point

    thread->context = ctx;

    spinlock_acquire(&scheduler_lock);
    thread->next = process->threads;
    process->threads = thread;
    spinlock_release(&scheduler_lock);

    return thread;
}

thread_t *get_current_thread(void)
{
    cpu_t *cpu = get_cpu();
    if (!cpu) return NULL;
    return cpu->active_thread;
}

process_t *get_current_process(void)
{
    thread_t *t = get_current_thread();
    if (t) return t->process;
    return NULL;
}

void schedule(void)
{
    // Save interrupt state and disable interrupts
    uint64_t rflags;
    __asm__ volatile("pushfq; pop %0; cli" : "=r"(rflags));

    cpu_t *cpu = get_cpu();
    thread_t *curr = cpu->active_thread;

    if (!curr)
    {
        if (rflags & 0x200)
            __asm__ volatile("sti");
        return;
    }

    spinlock_acquire(&scheduler_lock);

    thread_t *next_thread = NULL;
    process_t *p = curr->process;
    thread_t *t = curr->next;

    // Pass 1: Search from current->next to end of all lists.
    while (p)
    {
        while (t)
        {
            if (t->state == THREAD_READY)
            {
                next_thread = t;
                goto found;
            }
            t = t->next;
        }
        p = p->next;
        if (p)
            t = p->threads;
    }

    // Pass 2: Search from beginning of all lists to current.
    p = process_list;
    while (p)
    {
        t = p->threads;
        while (t)
        {
            if (t == curr)
                goto check_done; // Reached current, stop
            if (t->state == THREAD_READY)
            {
                next_thread = t;
                goto found;
            }
            t = t->next;
        }
        p = p->next;
    }

check_done:
found:
    if (next_thread && next_thread != curr)
    {
        thread_t *prev = curr;

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

        // Save FPU state of previous thread
        save_fpu_state(&prev->fpu_state);
        // Restore FPU state of next thread
        restore_fpu_state(&next_thread->fpu_state);

        cpu->active_thread = next_thread;
        next_thread->state = THREAD_RUNNING;
        if (prev->state == THREAD_RUNNING)
            prev->state = THREAD_READY;
        
        switch_to(prev, next_thread);
        
        // Lock is released by the new thread (either thread_trampoline or here)
        spinlock_release(&scheduler_lock);
    }
    else
    {
        spinlock_release(&scheduler_lock);
    }

    // Restore interrupt state
    if (rflags & 0x200)
        __asm__ volatile("sti");
}

void yield(void)
{
    schedule();
}
