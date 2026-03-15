#include <sys/syscall.h>
#include <sys/wait.h>
#include <syscall_common.h>

int sys_waitpid(int pid, int *status, int options)
{
#ifdef TEST_MODE
    printk("sys_waitpid: pid=%d waiting for %d...\n", current_process ? current_process->pid : -1, pid);
#endif
    if (options & ~WNOHANG)
        return -1;
    if (pid < -1 || pid == 0)
        return -1;
    if (status) {
        int user_status = require_user_ptr_write(status, sizeof(*status), "sys_waitpid status", -1);
        if (user_status != 0)
            return user_status;
    }

    while (1) {
        uint64_t rflags;
        SPIN_LOCK_INT_SAVE(scheduler_lock, rflags);

        sigaction_t action = current_process->sigactions[SIGCHLD - 1];
        if (action.sa_handler == SIG_IGN || (action.sa_flags & SA_NOCLDWAIT)) {
            SPIN_UNLOCK_INT_RESTORE(scheduler_lock, rflags);
            return -1;
        }

        if (pid == -1) {
            bool has_children = false;
            bool has_unreapable_zombie = false;
            process_t *found = nullptr;
            process_t *p;
            list_foreach_entry(p, &process_list, list) {
                if (p->parent != current_process || p->auto_reap)
                    continue;
                has_children = true;
                if (p->terminated) {
                    if (process_can_reap_locked(p)) {
                        found = p;
                        break;
                    }
                    has_unreapable_zombie = true;
                }
            }

            if (found) {
                int code = found->exit_code;
                if (status) {
                    int user_status = copy_to_user_checked(status, &code, sizeof(code), "sys_waitpid status", -1);
                    if (user_status != 0) {
                        SPIN_UNLOCK_INT_RESTORE(scheduler_lock, rflags);
                        return user_status;
                    }
                }
                const int found_pid = found->pid;
                SPIN_UNLOCK_INT_RESTORE(scheduler_lock, rflags);
                process_reap(found);
                TEST_SYSCALL_LOG("sys_waitpid: pid=%d reaped child=%d code=%d\n",
                                 current_process ? current_process->pid : -1,
                                 found_pid,
                                 code);
                return found_pid;
            }

            if (!has_children || (options & WNOHANG)) {
                SPIN_UNLOCK_INT_RESTORE(scheduler_lock, rflags);
                return has_children ? 0 : -1;
            }

            if (has_unreapable_zombie) {
                SPIN_UNLOCK_INT_RESTORE(scheduler_lock, rflags);
                yield();
                continue;
            }

            TEST_SYSCALL_LOG("sys_waitpid: pid=%d sleeping for child\n",
                             current_process ? current_process->pid : -1);
            thread_sleep(current_process, &scheduler_lock);
            SPIN_UNLOCK_INT_RESTORE(scheduler_lock, rflags);
            continue;
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
                    int user_status = copy_to_user_checked(status, &code, sizeof(code), "sys_waitpid status", -1);
                    if (user_status != 0) {
                        SPIN_UNLOCK_INT_RESTORE(scheduler_lock, rflags);
                        return user_status;
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
            if (options & WNOHANG)
                return 0;
            yield();
            continue;
        }

        if (options & WNOHANG) {
            SPIN_UNLOCK_INT_RESTORE(scheduler_lock, rflags);
            return 0;
        }

        TEST_SYSCALL_LOG("sys_waitpid: pid=%d sleeping for child\n",
                         current_process ? current_process->pid : -1);
        thread_sleep(current_process, &scheduler_lock);
        SPIN_UNLOCK_INT_RESTORE(scheduler_lock, rflags);
    }
}
