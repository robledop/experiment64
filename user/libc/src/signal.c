#include <signal.h>
#include <sys/syscall.h>
#include <util.h>

extern void __signal_trampoline(void);

int sigaction(const int signum, const struct sigaction *act, struct sigaction *oldact)
{
    if (!act) {
        return clamp_signed_to_int(syscall3(SYS_SIGACTION, signum, 0, (long)oldact));
    }

    struct sigaction local = *act;
    local.sa_restorer      = __signal_trampoline;
    return clamp_signed_to_int(syscall3(SYS_SIGACTION, signum, (long)&local, (long)oldact));
}

sighandler_t signal(const int signum, const sighandler_t handler)
{
    struct sigaction act = {};
    struct sigaction old = {};

    act.sa_handler  = handler;
    act.sa_mask     = 0;
    act.sa_flags    = 0;
    act.sa_restorer = __signal_trampoline; // The trampoline is used to return to sys_sigreturn after it runs

    if (sigaction(signum, &act, &old) < 0) {
        return SIG_ERR;
    }
    return old.sa_handler;
}

int sigprocmask(const int how, const sigset_t *set, sigset_t *oldset)
{
    return clamp_signed_to_int(syscall3(SYS_SIGPROCMASK, how, (long)set, (long)oldset));
}
