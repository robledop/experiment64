#include <sys/syscall.h>
#include <sys/wait.h>
#include <syscall_common.h>

/**
 * @brief Try to reap a single terminated child, while `scheduler_lock` is held.
 *
 * Copies out @p status and @p info if non-null. Releases @p rflags on error
 * so the caller can return without leaving interrupts disabled.
 *
 * @return 0 to continue looping in the caller, or the value to return from
 *         the syscall (>=0 for the reaped pid, <0 for a user-copy error).
 */
static int wait_reap_locked(process_t *found, int *status, crash_info_t *info,
                            uint64_t rflags, const char *op)
{
    const int code = found->exit_code;
    const crash_info_t ci = found->crash_info;
    if (status) {
        const int r = copy_to_user_checked(status, &code, sizeof(code), op, -1);
        if (r != 0) {
            SPIN_UNLOCK_INT_RESTORE(scheduler_lock, rflags);
            return r;
        }
    }
    if (info) {
        const int r = copy_to_user_checked(info, &ci, sizeof(ci), op, -1);
        if (r != 0) {
            SPIN_UNLOCK_INT_RESTORE(scheduler_lock, rflags);
            return r;
        }
    }
    const int pid = found->pid;
    SPIN_UNLOCK_INT_RESTORE(scheduler_lock, rflags);
    process_reap(found);
    TEST_SYSCALL_LOG("%s: pid=%d reaped child=%d code=%d\n",
                     op,
                     current_process ? current_process->pid : -1,
                     pid,
                     code);
    return pid;
}

/**
 * @brief Find a reapable terminated child (pid == -1 case).
 *
 * Walks @c process_list under the held scheduler_lock. Sets @p has_children
 * if the caller has any non-auto-reap child, and @p has_unreapable_zombie if
 * a terminated child exists but cannot yet be reaped (e.g. because a thread
 * is still active on another CPU).
 */
static process_t *wait_find_any_child(bool *has_children, bool *has_unreapable_zombie)
{
    *has_children = false;
    *has_unreapable_zombie = false;

    process_t *p;
    list_foreach_entry(p, &process_list, list) {
        if (p->parent != current_process || p->auto_reap)
            continue;
        *has_children = true;
        if (!p->terminated)
            continue;
        if (process_can_reap_locked(p))
            return p;
        *has_unreapable_zombie = true;
    }
    return nullptr;
}

/** Find a specific child by pid; returns nullptr if not a child of current. */
static process_t *wait_find_specific_child(int pid)
{
    process_t *p;
    list_foreach_entry(p, &process_list, list) {
        if (p->pid == pid)
            return p;
    }
    return nullptr;
}

int wait_for_child(int pid, int *status, int options, crash_info_t *info, const char *op)
{
    if (options & ~WNOHANG)
        return -1;
    if (pid < -1 || pid == 0)
        return -1;
    if (status) {
        const int r = require_user_ptr_write(status, sizeof(*status), op, -1);
        if (r != 0)
            return r;
    }
    if (info) {
        const int r = require_user_ptr_write(info, sizeof(*info), op, -1);
        if (r != 0)
            return r;
    }

    while (1) {
        uint64_t rflags;
        SPIN_LOCK_INT_SAVE(scheduler_lock, rflags);

        const sigaction_t action = current_process->sigactions[SIGCHLD - 1];
        if (action.sa_handler == SIG_IGN || (action.sa_flags & SA_NOCLDWAIT)) {
            SPIN_UNLOCK_INT_RESTORE(scheduler_lock, rflags);
            return -1;
        }

        if (pid == -1) {
            bool has_children = false;
            bool has_unreapable_zombie = false;
            process_t *found = wait_find_any_child(&has_children, &has_unreapable_zombie);

            if (found)
                return wait_reap_locked(found, status, info, rflags, op);

            if (!has_children || (options & WNOHANG)) {
                SPIN_UNLOCK_INT_RESTORE(scheduler_lock, rflags);
                return has_children ? 0 : -1;
            }

            if (has_unreapable_zombie) {
                SPIN_UNLOCK_INT_RESTORE(scheduler_lock, rflags);
                yield();
                continue;
            }

            TEST_SYSCALL_LOG("%s: pid=%d sleeping for child\n",
                             op,
                             current_process ? current_process->pid : -1);
            thread_sleep(current_process, &scheduler_lock);
            SPIN_UNLOCK_INT_RESTORE(scheduler_lock, rflags);
            continue;
        }

        // pid > 0: wait for a specific child.
        process_t *found = wait_find_specific_child(pid);
        if (!found || found->parent != current_process || found->auto_reap) {
            SPIN_UNLOCK_INT_RESTORE(scheduler_lock, rflags);
            return -1;
        }

        if (found->terminated) {
            if (process_can_reap_locked(found))
                return wait_reap_locked(found, status, info, rflags, op);

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

        TEST_SYSCALL_LOG("%s: pid=%d sleeping for child\n",
                         op,
                         current_process ? current_process->pid : -1);
        thread_sleep(current_process, &scheduler_lock);
        SPIN_UNLOCK_INT_RESTORE(scheduler_lock, rflags);
    }
}
