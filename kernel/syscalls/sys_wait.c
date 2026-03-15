#include <syscall_common.h>

int sys_wait(int* status)
{
#ifdef TEST_MODE
    printk("sys_wait: pid=%d waiting...\n", current_process ? current_process->pid : -1);
#endif
    if (status) {
        int user_status = require_user_ptr_write(status, sizeof(*status), "sys_wait status", -1);
        if (user_status != 0)
            return user_status;
    }
    while (1)
    {
        uint64_t rflags;
        SPIN_LOCK_INT_SAVE(scheduler_lock, rflags);

        sigaction_t action = current_process->sigactions[SIGCHLD - 1];
        if (action.sa_handler == SIG_IGN || (action.sa_flags & SA_NOCLDWAIT)) {
            SPIN_UNLOCK_INT_RESTORE(scheduler_lock, rflags);
            return -1;
        }

        bool has_children = false;
        process_t* found = nullptr;
        bool has_unreapable_zombie = false;
        process_t* p;
        list_foreach_entry(p, &process_list, list)
        {
            if (p->parent == current_process)
            {
                if (p->auto_reap)
                    continue;
                has_children = true;
                if (p->terminated)
                {
                    if (process_can_reap_locked(p))
                    {
                        found = p;
                        break;
                    }
                    has_unreapable_zombie = true;
                }
            }
        }

        if (found)
        {
            int code = found->exit_code;
            if (status) {
                int user_status = copy_to_user_checked(status, &code, sizeof(code), "sys_wait status", -1);
                if (user_status != 0) {
                    SPIN_UNLOCK_INT_RESTORE(scheduler_lock, rflags);
                    return user_status;
                }
            }
            int pid = found->pid;

            SPIN_UNLOCK_INT_RESTORE(scheduler_lock, rflags);
            process_reap(found);
            TEST_SYSCALL_LOG("sys_wait: pid=%d reaped child=%d code=%d\n", current_process->pid, pid, code);
            return pid;
        }

        if (!has_children)
        {
            SPIN_UNLOCK_INT_RESTORE(scheduler_lock, rflags);
            return -1;
        }
        if (has_unreapable_zombie)
        {
            SPIN_UNLOCK_INT_RESTORE(scheduler_lock, rflags);
            yield();
            continue;
        }
        TEST_SYSCALL_LOG("sys_wait: pid=%d sleeping for child\n", current_process->pid);
        thread_sleep(current_process, &scheduler_lock);
        SPIN_UNLOCK_INT_RESTORE(scheduler_lock, rflags);
    }
}
