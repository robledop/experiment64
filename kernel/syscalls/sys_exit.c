#include <syscall_common.h>

void sys_exit(int code)
{
    TEST_SYSCALL_LOG("sys_exit: pid=%d code=%d (exit_hook=%p)\n", current_process->pid, code, syscall_exit_hook);

    if (syscall_exit_hook)
    {
        syscall_exit_hook(code);
    }
#ifdef TEST_MODE
    if (syscall_exit_hook&& current_process->parent) {
        printk("sys_exit: child pid=%d code=%d waking parent pid=%d\n",
               current_process->pid,
               code,
               current_process->parent ? current_process->parent->pid : -1);
    }
#endif
    TEST_SYSCALL_LOG("Process %d exited with code %d\n", current_process->pid, code);

    __asm__ volatile (
    "cli"
    )
    ;

    spinlock_acquire(&scheduler_lock);

    thread_t* self = current_thread;
    process_t* proc = current_process;
    if (self)
        self->state = THREAD_TERMINATED;

    bool proc_terminated = false;
    if (self && self->is_user && proc)
    {
        proc_terminated = true;
        thread_t* t;
        list_foreach_entry(t, &proc->threads, list)
        {
            t->state = THREAD_TERMINATED;
            t->exit_code = code;
        }
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