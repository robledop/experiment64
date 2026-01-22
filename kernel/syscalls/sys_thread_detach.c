#include <sys/syscall.h>
#include <syscall_common.h>

int sys_thread_detach(int tid)
{
    if (!current_thread || !current_thread->is_user || !current_process)
        return -1;

    if (tid <= 0)
        return -1;

    uint64_t rflags;
    SPIN_LOCK_INT_SAVE(scheduler_lock, rflags);

    thread_t* target = find_thread_by_tid(current_process, tid);
    if (!target || !target->is_user)
    {
        SPIN_UNLOCK_INT_RESTORE(scheduler_lock, rflags);
        return -1;
    }

    if (target->detached)
    {
        SPIN_UNLOCK_INT_RESTORE(scheduler_lock, rflags);
        return -1;
    }

    if (target->state == THREAD_TERMINATED && !thread_active_on_any_cpu(target))
    {
        list_del(&target->list);
        target->process = nullptr;
        SPIN_UNLOCK_INT_RESTORE(scheduler_lock, rflags);

        free_thread_resources(target);
        return 0;
    }

    target->detached = true;
    SPIN_UNLOCK_INT_RESTORE(scheduler_lock, rflags);

    thread_wakeup(target);
    return 0;
}
