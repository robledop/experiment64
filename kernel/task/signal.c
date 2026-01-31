#include <task/signal.h>
#include <arch/x86_64/cpu.h>
#include <lib/string.h>
#include <task/spinlock.h>
#include <syscall_common.h>
#include <debug.h>


static inline sigset_t signal_valid_mask(void)
{
    static_assert(SIG_MAX <= 64, "Signal number too large for signal mask");

    // The first SIG_MAX bits are valid
    return (SIG_MAX == 64) ? ~((sigset_t)0) : (((sigset_t)1 << SIG_MAX) - 1);
}

static inline sigset_t signal_bit(const int sig)
{
    return (sigset_t)1 << (sig - 1);
}

static bool signal_default_ignore(const int sig)
{
    return sig == SIGCHLD || sig == SIGCONT;
}

static bool signal_uncatchable(const int sig)
{
    return sig == SIGKILL || sig == SIGSTOP;
}

static bool signal_default_terminate(int sig)
{
    return !signal_default_ignore(sig);
}

static void signal_mark_threads_ready(process_t *target)
{
    spinlock_assert_held(&scheduler_lock);
    if (!target) {
        return;
    }

    thread_t *t;
    list_foreach_entry(t, &target->threads, list) {
        if (t->state == THREAD_BLOCKED)
            t->state = THREAD_READY;
    }
}

static void signal_terminate_locked(process_t *target, int sig, process_t **parent_out)
{
    spinlock_assert_held(&scheduler_lock);
    if (!target) {
        return;
    }

    target->exit_code  = 128 + sig;
    target->terminated = true;
    signal_send_sigchld(target);

    thread_t *t;
    list_foreach_entry(t, &target->threads, list) {
        t->state = THREAD_TERMINATED;
    }

    // Reparent orphaned process
    process_t *new_parent = init_process ? init_process : kernel_process;
    process_t *p;
    list_foreach_entry(p, &process_list, list) {
        if (p && p->parent == target) {
            p->parent = new_parent;
            if (p->terminated) {
                thread_wakeup(new_parent);
            }
        }
    }

    if (parent_out) {
        *parent_out = target->parent;
    }
}

void signal_init_process(process_t *proc)
{
    if (!proc)
        return;

    memset(proc->sigactions, 0, sizeof(proc->sigactions));
    proc->sig_mask     = 0;
    proc->sig_pending  = 0;
    proc->sig_inflight = 0;
}

void signal_reset_exec(process_t *proc)
{
    if (!proc)
        return;

    for (int i = 0; i < SIG_MAX; i++) {
        if (proc->sigactions[i].sa_handler != SIG_IGN)
            proc->sigactions[i].sa_handler = SIG_DFL;
        proc->sigactions[i].sa_mask     = 0;
        proc->sigactions[i].sa_flags    = 0;
        proc->sigactions[i].sa_restorer = nullptr;
    }
    proc->sig_mask     = 0;
    proc->sig_pending  = 0;
    proc->sig_inflight = 0;
}

void signal_copy_on_fork(process_t *child, const process_t *parent)
{
    if (!child || !parent)
        return;

    memcpy(child->sigactions, parent->sigactions, sizeof(child->sigactions));
    child->sig_mask     = parent->sig_mask;
    child->sig_pending  = 0;
    child->sig_inflight = 0;
}

/**
 * Extract one signal bit from the pending set
 */
static int signal_claim_pending_locked(process_t *proc, sigaction_t *action_out, sigset_t *bit_out)
{
    spinlock_assert_held(&scheduler_lock);
    if (!proc) {
        return 0;
    }

    sigset_t pending = proc->sig_pending & ~proc->sig_mask;
    for (int sig = 1; sig <= SIG_MAX; sig++) {
        sigset_t bit = signal_bit(sig);
        if ((pending & bit) == 0) {
            continue;
        }

        sigaction_t action = proc->sigactions[sig - 1];
        if (action.sa_handler == SIG_IGN || (action.sa_handler == SIG_DFL && signal_default_ignore(sig))) {
            proc->sig_pending &= ~bit; // Clear bit
            pending           &= ~bit; // Clear bit
            continue;
        }

        proc->sig_pending &= ~bit; // Clear bit
        if (action_out) {
            *action_out = action;
        }
        if (bit_out) {
            *bit_out = bit;
        }
        return sig;
    }
    return 0;
}

static bool signal_setup_user_frame(uint64_t user_rsp, const sigcontext_t *ctx,
                                    uint64_t restorer, uint64_t *out_rsp)
{
    if (!ctx || restorer == 0 || !out_rsp)
        return false;
    if (user_rsp < sizeof(sigcontext_t) + sizeof(uint64_t))
        return false;

    uint64_t sp          = user_rsp;
    sp                   -= sizeof(sigcontext_t);
    sp                   &= ~0xFul; // Clear bit
    uint64_t sigctx_addr = sp;
    sp                   -= sizeof(uint64_t);
    uint64_t ret_addr    = sp;

    if (!copy_to_user((void *)sigctx_addr, ctx, sizeof(*ctx)))
        return false;
    if (!copy_to_user((void *)ret_addr, &restorer, sizeof(restorer)))
        return false;

    *out_rsp = ret_addr;
    return true;
}

void signal_send_sigchld(process_t *process)
{
    spinlock_assert_held(&scheduler_lock);
    if (!process || !process->parent) {
        return;
    }
    process_t *parent  = process->parent;
    sigaction_t action = parent->sigactions[SIGCHLD - 1];
    if (action.sa_handler == SIG_IGN || action.sa_handler == SIG_DFL) {
        parent->sig_pending &= ~signal_bit(SIGCHLD); // Clear bit
    } else {
        parent->sig_pending |= signal_bit(SIGCHLD); // Set bit
        // Only send signal immediately if not masked
        if ((parent->sig_mask & signal_bit(SIGCHLD)) == 0) {
            signal_mark_threads_ready(parent);
        }
    }
}

int signal_send_pid(int pid, int sig)
{
    if (sig <= 0 || sig > SIG_MAX) {
        return -1;
    }

    uint64_t flags;
    SPIN_LOCK_INT_SAVE(scheduler_lock, flags);

    process_t *target = nullptr;
    list_item_t *pos;
    list_foreach(pos, &process_list) {
        process_t *p = list_entry(pos, process_t, list);
        if (p->pid == pid) {
            target = p;
            break;
        }
    }

    if (!target) {
        SPIN_UNLOCK_INT_RESTORE(scheduler_lock, flags);
        return -1;
    }

    if (target->pid <= 1 || (init_process && target == init_process)) {
        SPIN_UNLOCK_INT_RESTORE(scheduler_lock, flags);
        return -1;
    }

    sigaction_t action = target->sigactions[sig - 1];
    bool killed_self   = false;
    process_t *parent  = nullptr;

    // Terminate the process if the signal is uncatchable or is "default terminate" and a handler has not been set.
    // Do nothing (and clear the pending bit) if the signal is ignored or has a default ignore action.
    // Otherwise, set the pending bit and mark threads ready to handle the signal.
    if (signal_uncatchable(sig) || (action.sa_handler == SIG_DFL && signal_default_terminate(sig))) {
        signal_terminate_locked(target, sig, &parent);
        killed_self = (target == current_process);
    } else if (action.sa_handler == SIG_IGN || (action.sa_handler == SIG_DFL && signal_default_ignore(sig))) {
        target->sig_pending &= ~signal_bit(sig); // Clear bit
    } else {
        target->sig_pending |= signal_bit(sig); // Set bit
        // Only send signal immediately if not masked
        if ((target->sig_mask & signal_bit(sig)) == 0) {
            signal_mark_threads_ready(parent);
        }
    }

    spinlock_release(&scheduler_lock);

    if (parent) {
        thread_wakeup(parent);
    }

    if (killed_self) {
        schedule();
        if (flags & RFLAGS_IF) {
            __asm__ volatile ("sti");
        }
        return 0;
    }

    if (flags & RFLAGS_IF) {
        __asm__ volatile ("sti");
    }
    return 0;
}

bool signal_deliver_after_syscall(struct syscall_regs *regs, const uint64_t *ret)
{
    if (!regs || !ret) {
        return false;
    }

    process_t *proc = current_process;
    thread_t *t     = current_thread;
    if (!proc || !t || !t->is_user || proc->terminated) {
        return false;
    }

    uint64_t flags;
    SPIN_LOCK_INT_SAVE(scheduler_lock, flags);

    if (proc->sig_inflight) {
        SPIN_UNLOCK_INT_RESTORE(scheduler_lock, flags);
        return false;
    }

    sigaction_t action;
    sigset_t bit = 0;
    int sig      = signal_claim_pending_locked(proc, &action, &bit);
    if (sig == 0) {
        SPIN_UNLOCK_INT_RESTORE(scheduler_lock, flags);
        return false;
    }

    if (signal_uncatchable(sig) || (action.sa_handler == SIG_DFL && signal_default_terminate(sig))) {
        process_t *parent = nullptr;
        signal_terminate_locked(proc, sig, &parent);
        spinlock_release(&scheduler_lock);
        if (parent) {
            thread_wakeup(parent);
        }
        schedule();
        if (flags & RFLAGS_IF) {
            __asm__ volatile ("sti");
        }
        return true;
    }

    if (!action.sa_restorer) {
        process_t *parent = nullptr;
        signal_terminate_locked(proc, SIGSEGV, &parent);
        spinlock_release(&scheduler_lock);
        if (parent) {
            thread_wakeup(parent);
        }
        schedule();
        if (flags & RFLAGS_IF) {
            __asm__ volatile ("sti");
        }
        return true;
    }

    sigset_t old_mask  = proc->sig_mask;
    proc->sig_mask     = (proc->sig_mask | action.sa_mask | bit) & signal_valid_mask();
    proc->sig_inflight = sig;

    SPIN_UNLOCK_INT_RESTORE(scheduler_lock, flags);

    cpu_t *cpu        = get_cpu();
    uint64_t user_rsp = cpu ? cpu->user_rsp : 0;

    sigcontext_t ctx = {};
    ctx.r15          = regs->r15;
    ctx.r14          = regs->r14;
    ctx.r13          = regs->r13;
    ctx.r12          = regs->r12;
    ctx.r11          = regs->r11;
    ctx.r10          = regs->r10;
    ctx.r9           = regs->r9;
    ctx.r8           = regs->r8;
    ctx.rdi          = regs->rdi;
    ctx.rsi          = regs->rsi;
    ctx.rdx          = regs->rdx;
    ctx.rcx          = regs->rcx;
    ctx.rbx          = regs->rbx;
    ctx.rbp          = regs->rbp;
    ctx.rax          = *ret;
    ctx.rip          = regs->rcx;
    ctx.rflags       = regs->r11;
    ctx.rsp          = user_rsp;
    ctx.sigmask      = old_mask;

    uint64_t new_rsp = 0;
    if (!signal_setup_user_frame(user_rsp, &ctx, (uint64_t)action.sa_restorer, &new_rsp)) {
        uint64_t flags2;
        SPIN_LOCK_INT_SAVE(scheduler_lock, flags2);
        process_t *parent = nullptr;
        signal_terminate_locked(proc, SIGSEGV, &parent);
        spinlock_release(&scheduler_lock);
        if (parent) {
            thread_wakeup(parent);
        }
        schedule();
        if (flags2 & RFLAGS_IF) {
            __asm__ volatile ("sti");
        }
        return true;
    }

    regs->rcx = (uint64_t)action.sa_handler;
    regs->rdi = sig;
    if (cpu)
        cpu->user_rsp = new_rsp;
    t->saved_user_rsp = new_rsp;

    return true;
}

bool signal_deliver_after_interrupt(struct interrupt_frame *frame)
{
    if (!frame)
        return false;
    if ((frame->cs & 0x3) == 0) // Only in user-mode
        return false;

    process_t *proc = current_process;
    thread_t *t     = current_thread;
    if (!proc || !t || !t->is_user || proc->terminated) {
        return false;
    }

    uint64_t flags;
    SPIN_LOCK_INT_SAVE(scheduler_lock, flags);

    // Check sig_inflight after the lock to prevent race conditions
    // where two handlers can be delivered concurrently
    if (proc->sig_inflight) {
        SPIN_UNLOCK_INT_RESTORE(scheduler_lock, flags);
        return false;
    }

    sigaction_t action;
    sigset_t bit = 0;
    int sig      = signal_claim_pending_locked(proc, &action, &bit);
    if (sig == 0) {
        SPIN_UNLOCK_INT_RESTORE(scheduler_lock, flags);
        return false;
    }

    if (signal_uncatchable(sig) || (action.sa_handler == SIG_DFL && signal_default_terminate(sig))) {
        process_t *parent = nullptr;
        signal_terminate_locked(proc, sig, &parent);
        SPIN_UNLOCK_INT_RESTORE(scheduler_lock, flags);
        if (parent) {
            thread_wakeup(parent);
        }
        schedule();
        return true;
    }

    // If there's no sa_restorer, segfault.
    // sa_restorer is supposed to be set by libc.
    if (!action.sa_restorer) {
        panic("Signal handler missing sa_restorer");
        // process_t *parent = nullptr;
        // signal_terminate_locked(proc, SIGSEGV, &parent);
        // SPIN_UNLOCK_INT_RESTORE(scheduler_lock, flags);
        // if (parent) {
        //     thread_wakeup(parent);
        // }
        // schedule();
        // return true;
    }

    sigset_t old_mask  = proc->sig_mask;
    proc->sig_mask     = (proc->sig_mask | action.sa_mask | bit) & signal_valid_mask();
    proc->sig_inflight = sig;

    SPIN_UNLOCK_INT_RESTORE(scheduler_lock, flags);

    sigcontext_t ctx = {};
    ctx.r15          = frame->r15;
    ctx.r14          = frame->r14;
    ctx.r13          = frame->r13;
    ctx.r12          = frame->r12;
    ctx.r11          = frame->r11;
    ctx.r10          = frame->r10;
    ctx.r9           = frame->r9;
    ctx.r8           = frame->r8;
    ctx.rdi          = frame->rdi;
    ctx.rsi          = frame->rsi;
    ctx.rdx          = frame->rdx;
    ctx.rcx          = frame->rcx;
    ctx.rbx          = frame->rbx;
    ctx.rbp          = frame->rbp;
    ctx.rax          = frame->rax;
    ctx.rip          = frame->rip;
    ctx.rflags       = frame->rflags;
    ctx.rsp          = frame->rsp;
    ctx.sigmask      = old_mask;

    uint64_t new_rsp = 0;
    if (!signal_setup_user_frame(frame->rsp, &ctx, (uint64_t)action.sa_restorer, &new_rsp)) {
        uint64_t flags2;
        SPIN_LOCK_INT_SAVE(scheduler_lock, flags2);
        process_t *parent = nullptr;
        signal_terminate_locked(proc, SIGSEGV, &parent);
        SPIN_UNLOCK_INT_RESTORE(scheduler_lock, flags2);
        if (parent)
            thread_wakeup(parent);
        schedule();
        return true;
    }

    frame->rip = (uint64_t)action.sa_handler;
    frame->rdi = sig;
    frame->rsp = new_rsp;
    cpu_t *cpu = get_cpu();
    if (cpu)
        cpu->user_rsp = new_rsp;
    t->saved_user_rsp = new_rsp;

    return true;
}