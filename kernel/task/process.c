#include "process.h"
#include "heap.h"
#include "string.h"
#include "terminal.h"
#include "cpu.h"
#include "vmm.h"
#include "syscall.h"

extern uint64_t user_rsp_scratch;
extern uint64_t syscall_stack_top;

process_t *process_list = NULL;
process_t *current_process = NULL; // Need to maintain this
thread_t *current_thread = NULL;
static int next_pid = 1;
static int next_tid = 1;
volatile uint64_t scheduler_ticks = 0;

void scheduler_tick(void)
{
    scheduler_ticks++;

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
}

void process_init(void)
{
    // Initialize the first kernel process (idle task / initial kernel task)
    process_t *kernel_process = kmalloc(sizeof(process_t));
    if (!kernel_process)
    {
        printf("Process: Failed to allocate kernel process\n");
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
        printf("Process: Failed to allocate kernel thread\n");
        return;
    }
    memset(kernel_thread, 0, sizeof(thread_t));

    kernel_thread->tid = next_tid++;
    kernel_thread->process = kernel_process;
    kernel_thread->state = THREAD_RUNNING;

    // For the initial kernel thread, we assume we are running on a valid stack.
    // We can't easily determine the top of the current stack without more info.
    // But for now, this is just the bootstrap thread.
    kernel_thread->kstack_top = syscall_stack_top;

    kernel_process->threads = kernel_thread;
    process_list = kernel_process;
    current_thread = kernel_thread;
    current_process = kernel_process;

    printf("Process: Initialized kernel process PID %d\n", kernel_process->pid);
}

process_t *process_create(const char *name)
{
    process_t *proc = kmalloc(sizeof(process_t));
    if (!proc)
        return NULL;
    memset(proc, 0, sizeof(process_t));

    proc->pid = next_pid++;
    strncpy(proc->name, name, PROCESS_NAME_MAX - 1);
    if (current_process && current_process->cwd[0])
    {
        strncpy(proc->cwd, current_process->cwd, VFS_MAX_PATH - 1);
        proc->cwd[VFS_MAX_PATH - 1] = '\0';
    }
    else
    {
        proc->cwd[0] = '/';
        proc->cwd[1] = '\0';
    }

    proc->next = process_list;
    process_list = proc;

    return proc;
}

thread_t *thread_create(process_t *process, void (*entry)(void), bool is_user)
{
    thread_t *thread = kmalloc(sizeof(thread_t));
    if (!thread)
        return NULL;
    memset(thread, 0, sizeof(thread_t));

    thread->tid = next_tid++;
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
    // thread->rsp = thread->kstack_top; // No longer used directly

    uint64_t *stack_ptr = (uint64_t *)thread->kstack_top;

    extern void thread_trampoline(void);

    // Reserve space for context
    stack_ptr -= sizeof(struct context) / sizeof(uint64_t);
    struct context *ctx = (struct context *)stack_ptr;

    memset(ctx, 0, sizeof(struct context));
    ctx->rip = (uint64_t)thread_trampoline;
    ctx->r12 = (uint64_t)entry; // R12 holds entry point

    thread->context = ctx;

    (void)is_user; // TODO: Handle user mode stack setup if needed

    thread->next = process->threads;
    process->threads = thread;

    return thread;
}

thread_t *get_current_thread(void)
{
    return current_thread;
}

void schedule(void)
{
    // Save interrupt state and disable interrupts
    uint64_t rflags;
    __asm__ volatile("pushfq; pop %0; cli" : "=r"(rflags));

    if (!current_thread)
    {
        if (rflags & 0x200)
            __asm__ volatile("sti");
        return;
    }

    // printf("Schedule: current %d\n", current_thread->tid);

    thread_t *next_thread = NULL;
    process_t *p = current_thread->process;
    thread_t *t = current_thread->next;

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
            if (t == current_thread)
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
    if (next_thread && next_thread != current_thread)
    {
        thread_t *prev = current_thread;

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
        prev->saved_user_rsp = user_rsp_scratch;
        user_rsp_scratch = next_thread->saved_user_rsp;

        // Save FPU state of previous thread
        save_fpu_state(&prev->fpu_state);
        // Restore FPU state of next thread
        restore_fpu_state(&next_thread->fpu_state);

        current_thread = next_thread;
        current_process = current_thread->process;
        current_thread->state = THREAD_RUNNING;
        if (prev->state == THREAD_RUNNING)
            prev->state = THREAD_READY;
        // printf("Switching to %d\n", next_thread->tid);
        switch_to(prev, next_thread);
    }

    // Restore interrupt state
    if (rflags & 0x200)
        __asm__ volatile("sti");
}

void yield(void)
{
    schedule();
}
