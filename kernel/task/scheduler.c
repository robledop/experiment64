#include <task/process.h>
#include <task/signal.h>
#include "scheduler_internal.h"
#include <mem/heap.h>
#include <lib/string.h>
#include <drivers/terminal.h>
#include <mem/vmm.h>
#include <sys/syscall.h>
#include <arch/x86_64/apic.h>
#include <arch/x86_64/smp.h>
#include <arch/x86_64/gdt.h>
#include <debug.h>

#define TIME_SLICE_TICKS ((TIME_SLICE_MS * TIMER_FREQUENCY_HZ) / 1000)

spinlock_t scheduler_lock;
volatile uint64_t scheduler_ticks = 0;

thread_t *scheduler_idle_threads[SCHEDULER_MAX_CPUS]  = {nullptr};
process_t *scheduler_rr_last_proc[SCHEDULER_MAX_CPUS] = {nullptr};
static bool scheduler_ready              = false;

extern int next_pid;
extern int next_tid;
extern void thread_trampoline(void);

[[noreturn]] static void scheduler_loop(void);

[[noreturn]] static void idle_task(void)
{
    while (1) {
        __asm__ volatile("hlt");
    }
}

/*
 * Create an idle thread for a CPU. Unlike regular threads, idle threads
 * are NOT added to the process thread list to avoid scheduler confusion.
 */
static thread_t *create_idle_thread(void)
{
    thread_t *thread = kmalloc(sizeof(thread_t));
    if (!thread)
        panic("Failed to allocate idle thread");
    memset(thread, 0, sizeof(thread_t));

    void *kstack = kmalloc(KSTACK_SIZE);
    if (!kstack) {
        kfree(thread);
        return nullptr;
    }
    memset(kstack, 0, KSTACK_SIZE);

    thread->tid             = __atomic_fetch_add(&next_tid, 1, __ATOMIC_SEQ_CST);
    thread->process         = kernel_process;
    thread->state           = THREAD_READY;
    thread->is_idle         = true;
    thread->is_user         = false;
    thread->ticks_remaining = TIME_SLICE_TICKS;
    thread->kstack_top      = (uint64_t)kstack + KSTACK_SIZE;

    init_fpu_state(&thread->fpu_state);

    uint64_t stack_ptr  = thread->kstack_top - KSTACK_SYSCALL_HEADROOM;
    stack_ptr           -= sizeof(struct context);
    struct context *ctx = (struct context *)stack_ptr;
    memset(ctx, 0, sizeof(struct context));

    ctx->rip        = (uint64_t)thread_trampoline;
    ctx->r12        = (uint64_t)idle_task;
    thread->context = ctx;
    thread->rsp     = stack_ptr;

    // Initialize list node but do NOT add to any list
    list_init_head(&thread->list);

    return thread;
}

/**
 * Create a scheduler thread for a CPU.
 * Each scheduler thread is responsible for managing the scheduling of threads
 * on a specific CPU. It runs at a higher priority than regular threads
 *
 * @param cpu_idx CPU index
 * @return Scheduler thread or nullptr on failure
 */
static thread_t *create_scheduler_thread(uint32_t cpu_idx)
{
    thread_t *thread = kmalloc(sizeof(thread_t));
    if (!thread)
        return nullptr;
    memset(thread, 0, sizeof(thread_t));

    void *kstack = kmalloc(KSTACK_SIZE);
    if (!kstack) {
        kfree(thread);
        return nullptr;
    }
    memset(kstack, 0, KSTACK_SIZE);

    thread->tid             = -(1000 + (int)cpu_idx);
    thread->process         = kernel_process;
    thread->state           = THREAD_RUNNING;
    thread->is_idle         = false;
    thread->is_user         = false;
    thread->ticks_remaining = TIME_SLICE_TICKS;
    thread->kstack_top      = (uint64_t)kstack + KSTACK_SIZE;

    init_fpu_state(&thread->fpu_state);

    uint64_t stack_ptr = thread->kstack_top - KSTACK_SYSCALL_HEADROOM;
    stack_ptr          -= sizeof(struct context);
    // Align for direct C entry (scheduler_loop). SysV expects 16B alignment at call sites.
    stack_ptr &= ~0xFULL;
    auto ctx  = (struct context *)stack_ptr;
    memset(ctx, 0, sizeof(struct context));
    ctx->rip = (uint64_t)scheduler_loop;

    thread->context = ctx;
    thread->rsp     = stack_ptr;
    list_init_head(&thread->list);
    return thread;
}

void process_init(void)
{
    spinlock_init(&scheduler_lock);

    // Initialize the first kernel process (initial kernel task)
    kernel_process = kmalloc(sizeof(process_t));
    if (!kernel_process) {
        boot_message(ERROR, "Process: Failed to allocate kernel process");
        return;
    }
    memset(kernel_process, 0, sizeof(process_t));
    kernel_process->pid = next_pid++;
    strcpy(kernel_process->name, "kernel");
    kernel_process->cwd[0] = '/';
    kernel_process->cwd[1] = '\0';
    spinlock_init(&kernel_process->fd_lock);
    vm_area_init(kernel_process);
    signal_init_process(kernel_process);

    uint64_t cr3;
    __asm__ volatile ("mov %0, cr3" : "=r"(cr3));
    kernel_process->pml4 = (pml4_t)cr3;

    thread_t *kernel_thread = kmalloc(sizeof(thread_t));
    if (!kernel_thread) {
        boot_message(ERROR, "Process: Failed to allocate kernel thread");
        return;
    }
    memset(kernel_thread, 0, sizeof(thread_t));

    kernel_thread->tid             = next_tid++;
    kernel_thread->process         = kernel_process;
    kernel_thread->state           = THREAD_RUNNING;
    kernel_thread->ticks_remaining = TIME_SLICE_TICKS;
    kernel_thread->is_idle         = false;

    // For the initial kernel thread, capture the current RSP and derive a stack window
    // so scheduler sanity checks consider it in-bounds. This thread is already running
    // on whatever bootstrap stack the BSP used, so align that RSP rather than an
    // arbitrary per-CPU kernel stack pointer.
    cpu_t *cpu = get_cpu();
    if (!cpu) {
        boot_message(ERROR, "Process: Failed to get current CPU for kernel thread");
        return;
    }
    uint64_t curr_rsp;
    __asm__ volatile ("mov %0, rsp" : "=r"(curr_rsp));

    uint64_t aligned_base     = curr_rsp & ~(uint64_t)(KSTACK_SIZE - 1);
    kernel_thread->kstack_top = aligned_base + KSTACK_SIZE;
    kernel_thread->rsp        = curr_rsp;

    // If this CPU does not have a kernel stack pointer yet, seed it so syscalls have
    // something reasonable until threads switch away from the bootstrap stack.
    if (cpu && cpu->kernel_rsp == 0)
        cpu->kernel_rsp = kernel_thread->kstack_top;

    list_init_head(&kernel_process->threads);
    list_add_tail(&kernel_thread->list, &kernel_process->threads);

    list_add_tail(&kernel_process->list, &process_list);

    cpu_set_active_thread(cpu, kernel_thread);

    // Create per-CPU scheduler pseudo-threads (not in any runnable list)
    uint32_t cpu_count = smp_get_cpu_count();
    for (uint32_t i = 0; i < cpu_count && i < SCHEDULER_MAX_CPUS; i++) {
        thread_t *sched = create_scheduler_thread(i);
        cpu_t *c        = smp_get_cpu_by_index(i);
        if (!sched || !c) {
            boot_message(ERROR, "Process: Failed to create scheduler thread for CPU %d", i);
            continue;
        }
        c->scheduler_thread = sched;
    }

    // Create idle threads for all CPUs
    // These are NOT added to the thread list - they're only used when no other thread is ready
    for (uint32_t i = 0; i < cpu_count && i < SCHEDULER_MAX_CPUS; i++) {
        scheduler_idle_threads[i] = create_idle_thread();
        if (!scheduler_idle_threads[i]) {
            boot_message(ERROR, "Process: Failed to create idle thread for CPU %d", i);
        }
    }

    boot_message(INFO,
                 "Process: Initialized kernel process PID %d with %d idle threads",
                 kernel_process->pid,
                 cpu_count);
    __atomic_store_n(&scheduler_ready, true, __ATOMIC_RELEASE);

    smp_ap_scheduler_ready();
}

bool scheduler_is_ready(void)
{
    return __atomic_load_n(&scheduler_ready, __ATOMIC_ACQUIRE);
}

void smp_init_ap_scheduler(void)
{
    cpu_t *cpu       = get_cpu();
    uint32_t cpu_idx = (uint32_t)cpu->cpu_index;

    if (cpu_idx < SCHEDULER_MAX_CPUS && cpu->scheduler_thread) {
        thread_t *scheduler_thread = cpu->scheduler_thread;
        cpu_set_active_thread(cpu, scheduler_thread);
        scheduler_thread->state = THREAD_RUNNING;

        // Ensure the syscall / TSS stack uses the scheduler stack for this CPU.
        cpu->kernel_rsp = scheduler_thread->kstack_top;
        tss_set_stack(cpu->kernel_rsp);

        // The AP enters `ap_main` on a Limine-provided bootstrap stack, not on the
        // per-thread kernel stack. If we don't switch stacks here, the first time
        // this CPU gets preempted, we will save an out-of-range RSP into the scheduler
        // thread, and later scheduler validation will reject that thread.
        //
        // Switch onto the scheduler thread stack by performing a one-way context switch
        // from a synthetic "bootstrap" thread frame. We do NOT hold scheduler_lock here
        // because scheduler_loop() will acquire it at the start of each iteration.
        __asm__ volatile ("cli");
        thread_t bootstrap = {};
        bootstrap.tid      = -1;
        bootstrap.process  = kernel_process;
        bootstrap.state    = THREAD_RUNNING;
        switch_to(&bootstrap, scheduler_thread);
        __builtin_unreachable();
    }
}

/**
 * xv6-style scheduler loop running on a per-CPU scheduler pseudo-thread stack.
 * @note we do NOT keep scheduler_lock held while running normal threads.
 */
[[noreturn]] static void scheduler_loop(void)
{
    cpu_t *cpu = get_cpu();
    if (!cpu)
        hcf();

    thread_t *scheduler_thread = cpu->scheduler_thread;
    if (!scheduler_thread) {
        hcf();
    }

    cpu_set_active_thread(cpu, scheduler_thread);
    cpu->user_rsp = 0;

    for (;;) {
        spinlock_acquire(&scheduler_lock);

        // Processes that have the SA_NOCLDWAIT flag set for the SIGCHLD signal
        // have their children automatically reaped
        process_t *reap_proc = scheduler_claim_auto_reap_locked();
        if (reap_proc) {
            spinlock_release(&scheduler_lock);
            process_reap(reap_proc);
            continue;
        }
        list_item_t free_list = LIST_HEAD_INIT(free_list);
        scheduler_collect_detached_terminated_threads(&free_list);
        thread_t *next = scheduler_find_next_thread_locked(cpu);
        if (!next) {
            spinlock_release(&scheduler_lock);
            scheduler_release_thread_list(&free_list);
            __asm__ volatile ("sti; hlt; cli");
            continue;
        }

        if (!scheduler_validate_next_thread_locked(next, "scheduler_loop")) {
            spinlock_release(&scheduler_lock);
            scheduler_release_thread_list(&free_list);
            continue;
        }

        if (next->process && next->process->pml4) {
            vmm_switch_pml4(next->process->pml4);
        }

        syscall_set_stack(next->kstack_top);

        cpu->user_rsp = next->saved_user_rsp;
        wrfsbase(next->fs_base);
        restore_fpu_state(&next->fpu_state);

        cpu_set_active_thread(cpu, next);
        next->state           = THREAD_RUNNING;
        next->ticks_remaining = TIME_SLICE_TICKS;

        spinlock_release(&scheduler_lock);
        scheduler_release_thread_list(&free_list);
        switch_to(scheduler_thread, next);

        vmm_switch_pml4(kernel_process->pml4);
        cpu_set_active_thread(cpu, scheduler_thread);
        cpu->user_rsp = 0;
    }
}

void schedule(void)
{
    // Save interrupt state and disable interrupts
    uint64_t rflags;
    __asm__ volatile ("pushfq; pop %0; cli" : "=r"(rflags));

    cpu_t *cpu                 = get_cpu();
    thread_t *curr             = cpu ? cpu->active_thread : nullptr;
    thread_t *scheduler_thread = cpu ? cpu->scheduler_thread : nullptr;
    if (!curr || !scheduler_thread) {
        if (rflags & RFLAGS_IF)
            __asm__ volatile ("sti");
        return;
    }

    spinlock_acquire(&scheduler_lock);

    // If we are preempting a running thread (e.g., from timer interrupt),
    // mark it runnable so the scheduler can pick it again.
    if (curr != scheduler_thread && curr->state == THREAD_RUNNING && !curr->is_idle) {
        thread_state_store(curr, THREAD_READY);
        scheduler_thread_list_move_to_tail(curr);
    }

    curr->saved_user_rsp = cpu ? cpu->user_rsp : 0;
    curr->fs_base = rdfsbase();
    save_fpu_state(&curr->fpu_state);

    spinlock_release(&scheduler_lock);
    switch_to(curr, scheduler_thread);

    if (rflags & RFLAGS_IF)
        __asm__ volatile ("sti");
}
