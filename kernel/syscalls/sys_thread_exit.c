#include <sys/syscall.h>
#include <syscall_common.h>

void sys_thread_exit(int code)
{
    thread_t *self  = current_thread;
    process_t *proc = current_process;
    if (!self || !self->is_user || !proc)
        return;

    TEST_SYSCALL_LOG("sys_thread_exit: pid=%d tid=%d code=%d\n",
                     proc->pid,
                     self->tid,
                     code);

    __asm__ volatile ("cli");

    const uint64_t stack_start = self->user_stack_base;
    const uint64_t stack_end   = self->user_stack_top;
    if (stack_start != 0 && stack_end > stack_start)
        sys_munmap((void *)stack_start, stack_end - stack_start);

    spinlock_acquire(&scheduler_lock);

    self->exit_code = code;
    self->state     = THREAD_TERMINATED;

    bool last_thread = true;
    thread_t *t;
    list_foreach_entry(t, &proc->threads, list) {
        if (t != self && t->state != THREAD_TERMINATED) {
            last_thread = false;
            break;
        }
    }

    process_t *parent = nullptr;
    if (last_thread) {
        process_mark_exited_locked(proc, code, &parent);
    } else if (self->detached) {
        scheduler_enqueue_terminated(self);
    }

    spinlock_release(&scheduler_lock);

    thread_wakeup(self);

    if (parent)
        thread_wakeup(parent);

    schedule();
}