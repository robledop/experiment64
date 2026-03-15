#include <signal.h>
#include <sys/syscall.h>
#include <util.h>
#include <errno.h>
#include <unistd.h>
#include <status.h>

extern void __signal_trampoline(void);

static bool signal_signum_valid(const int signum)
{
    return signum > 0 && signum <= SIG_MAX;
}

static sigset_t signal_valid_mask(void)
{
    return (SIG_MAX == 64) ? ~((sigset_t)0) : (((sigset_t)1 << SIG_MAX) - 1);
}

static int signal_syscall_to_int(const long ret)
{
    if (ret < 0) {
        errno = (int)-ret;
        return -1;
    }
    return clamp_signed_to_int(ret);
}

int sigaction(const int signum, const struct sigaction *act, struct sigaction *oldact)
{
    if (!act) {
        return signal_syscall_to_int(syscall3(SYS_SIGACTION, signum, 0, (long)oldact));
    }

    struct sigaction local = *act;
    local.sa_restorer      = __signal_trampoline;
    return signal_syscall_to_int(syscall3(SYS_SIGACTION, signum, (long)&local, (long)oldact));
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
    return signal_syscall_to_int(syscall3(SYS_SIGPROCMASK, how, (long)set, (long)oldset));
}

int sigemptyset(sigset_t *set)
{
    if (!set) {
        errno = EINVAL;
        return -1;
    }
    *set = 0;
    return 0;
}

int sigfillset(sigset_t *set)
{
    if (!set) {
        errno = EINVAL;
        return -1;
    }
    *set = signal_valid_mask();
    return 0;
}

int sigaddset(sigset_t *set, const int signum)
{
    if (!set || !signal_signum_valid(signum)) {
        errno = EINVAL;
        return -1;
    }
    *set |= ((sigset_t)1 << (signum - 1));
    return 0;
}

int sigdelset(sigset_t *set, const int signum)
{
    if (!set || !signal_signum_valid(signum)) {
        errno = EINVAL;
        return -1;
    }
    *set &= ~((sigset_t)1 << (signum - 1));
    return 0;
}

int sigismember(const sigset_t *set, const int signum)
{
    if (!set || !signal_signum_valid(signum)) {
        errno = EINVAL;
        return -1;
    }
    return (*set & ((sigset_t)1 << (signum - 1))) ? 1 : 0;
}

int sigsuspend(const sigset_t *sigmask)
{
    (void)sigmask;
    errno = EUNIMP;
    return -1;
}

int raise(int sig)
{
    return kill(getpid(), sig);
}

static const char *signal_names[] = {
    [SIGHUP]  = "Hangup",
    [SIGINT]  = "Interrupt",
    [SIGQUIT] = "Quit",
    [SIGILL]  = "Illegal instruction",
    [SIGTRAP] = "Trace/breakpoint trap",
    [SIGABRT] = "Aborted",
    [SIGBUS]  = "Bus error",
    [SIGFPE]  = "Floating point exception",
    [SIGKILL] = "Killed",
    [SIGUSR1] = "User defined signal 1",
    [SIGSEGV] = "Segmentation fault",
    [SIGUSR2] = "User defined signal 2",
    [SIGPIPE] = "Broken pipe",
    [SIGALRM] = "Alarm clock",
    [SIGTERM] = "Terminated",
    [SIGCHLD] = "Child exited",
    [SIGCONT] = "Continued",
    [SIGSTOP] = "Stopped (signal)",
    [SIGTSTP] = "Stopped",
    [SIGTTIN] = "Stopped (tty input)",
    [SIGTTOU] = "Stopped (tty output)",
    [SIGWINCH] = "Window changed",
};

char *strsignal(int sig)
{
    if (sig > 0 && sig < (int)(sizeof(signal_names) / sizeof(signal_names[0])) && signal_names[sig])
        return (char *)signal_names[sig];
    return "Unknown signal";
}
