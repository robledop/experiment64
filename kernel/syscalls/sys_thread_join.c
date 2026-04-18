#include <sys/syscall.h>
#include <syscall_common.h>
#include <status.h>

int sys_thread_join(int tid, int* status)
{
    if (!current_thread || !current_thread->is_user || !current_process)
        return -EPERM;

    if (tid <= 0)
        return -ESRCH;
    if (status) {
        int user_status = require_user_ptr_write(status, sizeof(*status), "sys_thread_join status", -EFAULT);
        if (user_status != 0)
            return user_status;
    }

    for (;;)
    {
        thread_t *target    = nullptr;
        int exit_code       = 0;
        enum { LOOP_RETRY, LOOP_YIELD_RETRY, LOOP_REAP } next;

        WITH_LOCK(scheduler_lock) {
            target = find_thread_by_tid(current_process, tid);
            if (!target || !target->is_user)
                return -ESRCH;

            if (target == current_thread)
                return -EDEADLK;

            if (target->state == THREAD_BLOCKED && target->chan == current_thread)
                return -EDEADLK;

            if (target->detached)
                return -EINVAL;

            if (target->state != THREAD_TERMINATED) {
                thread_sleep(target, &scheduler_lock);
                next = LOOP_RETRY;
                break;
            }

            if (thread_active_on_any_cpu(target)) {
                next = LOOP_YIELD_RETRY;
                break;
            }

            exit_code       = target->exit_code;
            list_del(&target->list);
            target->process = nullptr;
            next            = LOOP_REAP;
        }

        if (next == LOOP_RETRY)
            continue;
        if (next == LOOP_YIELD_RETRY) {
            yield();
            continue;
        }

        // LOOP_REAP: target dequeued, lock released, finish up.
        if (status)
        {
            int user_status = copy_to_user_checked(status, &exit_code, sizeof(exit_code), "sys_thread_join status",
                                                   -EFAULT);
            if (user_status != 0)
            {
                free_thread_resources(target);
                return user_status;
            }
        }

        free_thread_resources(target);
        return 0;
    }
}
