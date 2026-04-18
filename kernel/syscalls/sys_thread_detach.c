#include <sys/syscall.h>
#include <syscall_common.h>
#include <status.h>

int sys_thread_detach(int tid)
{
    if (!current_thread || !current_thread->is_user || !current_process)
        return -EPERM;

    if (tid <= 0)
        return -ESRCH;

    thread_t *target    = nullptr;
    bool needs_free     = false;
    WITH_LOCK(scheduler_lock) {
        target = find_thread_by_tid(current_process, tid);
        if (!target || !target->is_user)
            return -ESRCH;

        if (target->detached)
            return -EINVAL;

        if (target->state == THREAD_TERMINATED && !thread_active_on_any_cpu(target)) {
            list_del(&target->list);
            target->process = nullptr;
            needs_free      = true;
            break;
        }

        target->detached = true;
    }

    if (needs_free) {
        free_thread_resources(target);
        return 0;
    }

    thread_wakeup(target);
    return 0;
}
