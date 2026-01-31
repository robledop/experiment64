#include <sys/syscall.h>
#include <syscall_common.h>

int sys_waitpid(int pid, int *status, int options)
{
#ifdef TEST_MODE
    printk("sys_waitpid: pid=%d waiting for %d...\n", current_process ? current_process->pid : -1, pid);
#endif
    if (options != 0)
        return -1;
    if (pid == -1)
        return sys_wait(status);
    if (pid <= 0)
        return -1;
    if (status && !user_ptr_write_ok(status, sizeof(*status), "sys_waitpid status"))
        return -1;

    while (1) {
        uint64_t rflags;
        SPIN_LOCK_INT_SAVE(scheduler_lock, rflags);

        sigaction_t action = current_process->sigactions[SIGCHLD - 1];
        if (action.sa_handler == SIG_IGN || (action.sa_flags & SA_NOCLDWAIT)) {
            SPIN_UNLOCK_INT_RESTORE(scheduler_lock, rflags);
            return -1;
        }

        process_t *found = nullptr;
        process_t *p;
        list_foreach_entry(p, &process_list, list) {
            if (p->pid == pid) {
                found = p;
                break;
            }
        }

        if (!found || found->parent != current_process || found->auto_reap) {
            SPIN_UNLOCK_INT_RESTORE(scheduler_lock, rflags);
            return -1;
        }

        if (found->terminated) {
            if (process_can_reap_locked(found)) {
                int code = found->exit_code;
                if (status) {
                    if (!copy_to_user(status, &code, sizeof(int))) {
                        SPIN_UNLOCK_INT_RESTORE(scheduler_lock, rflags);
                        return -1;
                    }
                }
                SPIN_UNLOCK_INT_RESTORE(scheduler_lock, rflags);
                process_reap(found);
                TEST_SYSCALL_LOG("sys_waitpid: pid=%d reaped child=%d code=%d\n",
                                 current_process ? current_process->pid : -1,
                                 pid,
                                 code);
                return pid;
            }

            SPIN_UNLOCK_INT_RESTORE(scheduler_lock, rflags);
            yield();
            continue;
        }

        TEST_SYSCALL_LOG("sys_waitpid: pid=%d sleeping for child\n",
                         current_process ? current_process->pid : -1);
        thread_sleep(current_process, &scheduler_lock);
        SPIN_UNLOCK_INT_RESTORE(scheduler_lock, rflags);
    }
}