#include <arch/x86_64/cpu.h>
#include <lib/string.h>
#include <task/process.h>

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
