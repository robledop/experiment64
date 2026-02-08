#include <task/process.h>
#include <mem/heap.h>
#include <lib/string.h>
#include <drivers/terminal.h>
#include <arch/x86_64/apic.h>

#define TIME_SLICE_TICKS ((TIME_SLICE_MS * TIMER_FREQUENCY_HZ) / 1000)

int next_tid = 1;

extern void thread_trampoline(void);

thread_t *thread_create(process_t *process, void (*entry)(void), bool is_user)
{
    thread_t *thread = kmalloc(sizeof(thread_t));
    if (!thread)
        return nullptr;
    memset(thread, 0, sizeof(thread_t));

    spinlock_acquire(&scheduler_lock);
    thread->tid = next_tid++;
    spinlock_release(&scheduler_lock);

    thread->process         = process;
    thread->is_user         = is_user;
    thread->ticks_remaining = TIME_SLICE_TICKS;
    thread_state_store(thread, THREAD_BLOCKED);

    init_fpu_state(&thread->fpu_state);

    void *stack = kmalloc(KSTACK_SIZE);
    if (!stack) {
        kfree(thread);
        return nullptr;
    }
    thread->kstack_top = (uint64_t)stack + KSTACK_SIZE;

    // Reserve the very top of the stack for syscall entry pushes so they don't
    // clobber the context-switch frame we place near the top.
    auto stack_ptr = (uint64_t *)(thread->kstack_top - KSTACK_SYSCALL_HEADROOM);

    // Reserve space for context
    stack_ptr -= sizeof(struct context) / sizeof(uint64_t);
    auto ctx  = (struct context *)stack_ptr;

    memset(ctx, 0, sizeof(struct context));
    ctx->rip = (uint64_t)thread_trampoline;
    ctx->r12 = (uint64_t)entry;

    thread->context = ctx;
    thread->rsp     = (uint64_t)ctx;

    uint64_t rflags;
    SPIN_LOCK_INT_SAVE(scheduler_lock, rflags);
    list_add_tail(&thread->list, &process->threads);
    if (!is_user)
        thread_state_store(thread, THREAD_READY);
    SPIN_UNLOCK_INT_RESTORE(scheduler_lock, rflags);

    return thread;
}

void thread_make_ready(thread_t *thread)
{
    if (!thread)
        return;
    uint64_t rflags;
    SPIN_LOCK_INT_SAVE(scheduler_lock, rflags);
    thread_state_store(thread, THREAD_READY);
    SPIN_UNLOCK_INT_RESTORE(scheduler_lock, rflags);
}

thread_t *get_current_thread(void)
{
    cpu_t *cpu = get_cpu();
    if (!cpu)
        return nullptr;
    return cpu->active_thread;
}

process_t *get_current_process(void)
{
    thread_t *t = get_current_thread();
    if (t)
        return t->process;
    return nullptr;
}

void thread_sleep(void *chan, spinlock_t *lock)
{
    thread_t *curr = get_current_thread();
    if (!curr)
        return;

    // Save interrupt state and disable interrupts to avoid deadlock with scheduler_lock
    uint64_t rflags;
    __asm__ volatile ("pushfq; pop %0; cli" : "=r"(rflags));

    // Acquire scheduler lock for state transition; release any provided lock.
    const bool caller_had_scheduler_lock = (lock == &scheduler_lock);
    if (!caller_had_scheduler_lock) {
        spinlock_acquire(&scheduler_lock);
        if (lock)
            spinlock_release(lock);
    }

    curr->chan  = chan;
    curr->state = THREAD_BLOCKED;

    // Release scheduler lock so other threads can run while we sleep.
    spinlock_release(&scheduler_lock);

    schedule();

    curr->chan = nullptr;

    // Reacquire locks to restore caller expectations.
    if (!caller_had_scheduler_lock) {
        if (lock)
            spinlock_acquire(lock);
    } else {
        spinlock_acquire(&scheduler_lock);
    }

    if (rflags & RFLAGS_IF)
        __asm__ volatile ("sti");
}

void thread_wakeup(void *chan)
{
    uint64_t rflags;
    SPIN_LOCK_INT_SAVE(scheduler_lock, rflags);
    process_t *p;
    list_foreach_entry(p, &process_list, list) {
        thread_t *t;
        list_foreach_entry(t, &p->threads, list) {
            uint32_t raw_state = thread_state_load_raw(t);
            if (!thread_state_valid_raw(raw_state)) {
                boot_message(ERROR,
                             "thread_wakeup: invalid thread state pid=%d tid=%d state=%u",
                             p->pid,
                             t->tid,
                             raw_state);
                thread_state_store(t, THREAD_TERMINATED);
                continue;
            }

            auto state = (thread_state_t)raw_state;
            if (state == THREAD_BLOCKED && t->chan == chan) {
                thread_state_store(t, THREAD_READY);
                t->chan = nullptr;
            }
        }
    }
    SPIN_UNLOCK_INT_RESTORE(scheduler_lock, rflags);
}

int thread_wakeup_n(void *chan, process_t *scope, int max_count)
{
    if (max_count <= 0)
        return 0;

    uint64_t rflags;
    SPIN_LOCK_INT_SAVE(scheduler_lock, rflags);

    int woken = 0;
    if (scope) {
        thread_t *t;
        list_foreach_entry(t, &scope->threads, list) {
            uint32_t raw_state = thread_state_load_raw(t);
            if (!thread_state_valid_raw(raw_state)) {
                boot_message(ERROR,
                             "thread_wakeup_n: invalid thread state pid=%d tid=%d state=%u",
                             scope->pid,
                             t->tid,
                             raw_state);
                thread_state_store(t, THREAD_TERMINATED);
                continue;
            }

            if ((thread_state_t)raw_state == THREAD_BLOCKED && t->chan == chan) {
                thread_state_store(t, THREAD_READY);
                t->chan = nullptr;
                woken++;
                if (woken >= max_count)
                    break;
            }
        }
        SPIN_UNLOCK_INT_RESTORE(scheduler_lock, rflags);
        return woken;
    }

    process_t *p;
    list_foreach_entry(p, &process_list, list) {
        thread_t *t;
        list_foreach_entry(t, &p->threads, list) {
            uint32_t raw_state = thread_state_load_raw(t);
            if (!thread_state_valid_raw(raw_state)) {
                boot_message(ERROR,
                             "thread_wakeup_n: invalid thread state pid=%d tid=%d state=%u",
                             p->pid,
                             t->tid,
                             raw_state);
                thread_state_store(t, THREAD_TERMINATED);
                continue;
            }

            if ((thread_state_t)raw_state == THREAD_BLOCKED && t->chan == chan) {
                thread_state_store(t, THREAD_READY);
                t->chan = nullptr;
                woken++;
                if (woken >= max_count)
                    goto out;
            }
        }
    }

out:
    SPIN_UNLOCK_INT_RESTORE(scheduler_lock, rflags);
    return woken;
}

void yield(void)
{
    schedule();
}