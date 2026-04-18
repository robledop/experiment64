#include <sys/signal.h>
#include <task/process.h>
#include <arch/x86_64/cpu.h>
#include <syscall_common.h>
#include <sys/syscall.h>

uint64_t sys_sigreturn(const sigcontext_t* user_ctx, struct syscall_regs* regs)
{
    if (!user_ctx || !regs || !current_process)
        return -1;

    sigcontext_t ctx = {};
    if (!copy_from_user(&ctx, user_ctx, sizeof(ctx)))
        return -1;

    constexpr sigset_t valid_mask = (SIG_MAX >= 64) ? ~((sigset_t)0) : (((sigset_t)1 << SIG_MAX) - 1);

    WITH_LOCK(scheduler_lock) {
        current_process->sig_mask = ctx.sigmask & valid_mask;
        current_process->sig_inflight = 0;
    }

    regs->r15 = ctx.r15;
    regs->r14 = ctx.r14;
    regs->r13 = ctx.r13;
    regs->r12 = ctx.r12;
    regs->r10 = ctx.r10;
    regs->r9 = ctx.r9;
    regs->r8 = ctx.r8;
    regs->rdi = ctx.rdi;
    regs->rsi = ctx.rsi;
    regs->rdx = ctx.rdx;
    regs->rbx = ctx.rbx;
    regs->rbp = ctx.rbp;
    regs->rcx = ctx.rip;
    regs->r11 = ctx.rflags;

    cpu_t* cpu = get_cpu();
    if (cpu)
        cpu->user_rsp = ctx.rsp;

    thread_t* t = get_current_thread();
    if (t)
        t->saved_user_rsp = ctx.rsp;

    return ctx.rax;
}