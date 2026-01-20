#include <sys/signal.h>
#include <task/process.h>
#include <syscall_common.h>

int sys_sigaction(int signum, const sigaction_t* act, sigaction_t* oldact)
{
    if (!current_process)
        return -1;
    if (signum <= 0 || signum > SIG_MAX)
        return -1;
    if (signum == SIGKILL || signum == SIGSTOP)
    {
        if (act)
            return -1;
    }

    sigaction_t new_action = {};
    const bool has_new = (act != nullptr);
    if (has_new && !copy_from_user(&new_action, act, sizeof(new_action)))
        return -1;

    sigaction_t old_action = {};

    constexpr sigset_t valid_mask = (SIG_MAX >= 64) ? ~((sigset_t)0) : (((sigset_t)1 << SIG_MAX) - 1);
    sigset_t bit = (sigset_t)1 << (signum - 1);

    uint64_t flags;
    SPIN_LOCK_INT_SAVE(scheduler_lock, flags);
    old_action = current_process->sigactions[signum - 1];

    if (has_new)
    {
        if (new_action.sa_handler != SIG_DFL && new_action.sa_handler != SIG_IGN && !new_action.sa_restorer)
        {
            SPIN_UNLOCK_INT_RESTORE(scheduler_lock, flags);
            return -1;
        }

        new_action.sa_mask &= valid_mask;
        current_process->sigactions[signum - 1] = new_action;

        if (new_action.sa_handler == SIG_IGN)
            current_process->sig_pending &= ~bit;
    }
    SPIN_UNLOCK_INT_RESTORE(scheduler_lock, flags);

    if (oldact && !copy_to_user(oldact, &old_action, sizeof(old_action)))
        return -1;

    return 0;
}
