#include "syscall_common.h"

int sys_wait(int* status)
{
#ifdef TEST_MODE
    printk("sys_wait: pid=%d waiting...\n", current_process ? current_process->pid : -1);
#endif
    while (1)
    {
        uint64_t rflags;
        SPIN_LOCK_INT_SAVE(scheduler_lock, rflags);

        bool has_children = false;
        process_t* found = nullptr;
        bool has_unreapable_zombie = false;
        process_t* p;
        list_foreach_entry(p, &process_list, list)
        {
            if (p->parent == current_process)
            {
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
            if (status && (uint64_t)status < 0x800000000000)
            {
                copy_to_user(status, &code, sizeof(int));
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
