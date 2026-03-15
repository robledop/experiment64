#include <sys/signal.h>
#include <task/process.h>
#include <syscall_common.h>

int sys_sigprocmask(int how, const sigset_t *set, sigset_t *oldset)
{
    if (!current_process)
        return -1;

    if (set) {
        int status = require_user_ptr_read(set, sizeof(*set), "sys_sigprocmask set", -1);
        if (status != 0)
            return status;
    }
    if (oldset) {
        int status = require_user_ptr_write(oldset, sizeof(*oldset), "sys_sigprocmask oldset", -1);
        if (status != 0)
            return status;
    }

    sigset_t set_value = 0;
    if (set) {
        int status = copy_from_user_checked(&set_value, set, sizeof(set_value), "sys_sigprocmask set", -1);
        if (status != 0)
            return status;
    }

    sigset_t old_value = 0;
    constexpr sigset_t valid_mask = (SIG_MAX >= 64) ? ~((sigset_t)0) : (((sigset_t)1 << SIG_MAX) - 1);
    constexpr sigset_t unmaskable = ((sigset_t)1 << (SIGKILL - 1)) | ((sigset_t)1 << (SIGSTOP - 1));

    uint64_t flags;
    SPIN_LOCK_INT_SAVE(scheduler_lock, flags);
    old_value = current_process->sig_mask;

    if (set) {
        switch (how) {
        case SIG_BLOCK:
            current_process->sig_mask |= set_value;
            break;
        case SIG_UNBLOCK:
            current_process->sig_mask &= ~set_value;
            break;
        case SIG_SETMASK:
            current_process->sig_mask = set_value;
            break;
        default:
            SPIN_UNLOCK_INT_RESTORE(scheduler_lock, flags);
            return -1;
        }

        current_process->sig_mask &= valid_mask;
        current_process->sig_mask &= ~unmaskable;
    }

    SPIN_UNLOCK_INT_RESTORE(scheduler_lock, flags);

    if (oldset) {
        int status = copy_to_user_checked(oldset, &old_value, sizeof(old_value), "sys_sigprocmask oldset", -1);
        if (status != 0)
            return status;
    }

    return 0;
}
