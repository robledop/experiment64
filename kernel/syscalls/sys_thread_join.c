#include <sys/syscall.h>
#include <syscall_common.h>
#include <status.h>

int sys_thread_join(int tid, int* status)
{
    if (!current_thread || !current_thread->is_user || !current_process)
        return -EPERM;

    if (tid <= 0)
        return -ESRCH;
    if (status && !user_ptr_write_ok(status, sizeof(*status), "sys_thread_join status"))
        return -EFAULT;

    for (;;)
    {
        uint64_t rflags;
        SPIN_LOCK_INT_SAVE(scheduler_lock, rflags);

        thread_t* target = find_thread_by_tid(current_process, tid);
        if (!target || !target->is_user)
        {
            SPIN_UNLOCK_INT_RESTORE(scheduler_lock, rflags);
            return -ESRCH;
        }

        if (target == current_thread)
        {
            SPIN_UNLOCK_INT_RESTORE(scheduler_lock, rflags);
            return -EDEADLK;
        }

        if (target->state == THREAD_BLOCKED && target->chan == current_thread)
        {
            SPIN_UNLOCK_INT_RESTORE(scheduler_lock, rflags);
            return -EDEADLK;
        }

        if (target->detached)
        {
            SPIN_UNLOCK_INT_RESTORE(scheduler_lock, rflags);
            return -EINVAL;
        }

        if (target->state != THREAD_TERMINATED)
        {
            thread_sleep(target, &scheduler_lock);
            SPIN_UNLOCK_INT_RESTORE(scheduler_lock, rflags);
            continue;
        }

        if (thread_active_on_any_cpu(target))
        {
            SPIN_UNLOCK_INT_RESTORE(scheduler_lock, rflags);
            yield();
            continue;
        }

        const int exit_code = target->exit_code;

        list_del(&target->list);
        target->process = nullptr;
        SPIN_UNLOCK_INT_RESTORE(scheduler_lock, rflags);

        if (status)
        {
            if (!copy_to_user(status, &exit_code, sizeof(exit_code)))
            {
                free_thread_resources(target);
                return -EFAULT;
            }
        }

        free_thread_resources(target);
        return 0;
    }
}
