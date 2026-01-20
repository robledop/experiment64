#include <syscall_common.h>

#include <lib/string.h>
#include <mem/vmm.h>
#include <sys/syscall.h>
#include <task/signal.h>

extern void fork_child_trampoline(void);

int sys_fork(struct syscall_regs* regs)
{
    if (!regs)
        return -1;

    // Copy Address Space
    pml4_t child_pml4 = vmm_copy_pml4(current_process->pml4);
    if (!child_pml4)
        return -1;


    // Create Process
    process_t* child_proc = process_create(current_process->name);
    if (!child_proc)
    {
        vmm_destroy_pml4(child_pml4);
        return -1;
    }

    child_proc->pml4 = child_pml4;
    child_proc->parent = current_process;
    child_proc->heap_end = current_process->heap_end;
    signal_copy_on_fork(child_proc, current_process);

    process_copy_fds(child_proc, current_process);
    vm_area_clone(child_proc, current_process);

    // Create Thread
    thread_t* child_thread = thread_create(child_proc, nullptr, true);
    if (!child_thread)
    {
        process_destroy(child_proc);
        return -1;
    }

    // Setup Child Stack
    // Place the context-switch frame near the top of the kernel stack (like thread_create()).
    // We must also keep syscall_regs immediately above the context so fork_child_trampoline
    // starts with RSP == child_regs after switch_to's pops+ret.
    uint64_t ctx_addr = child_thread->kstack_top;
    ctx_addr -= KSTACK_SYSCALL_HEADROOM;
    ctx_addr -= sizeof(struct syscall_regs);
    ctx_addr -= sizeof(struct context);
    ctx_addr &= ~0xFULL; // keep 16-byte alignment so child_regs ends up 8 mod 16

    struct context* child_ctx = (struct context*)ctx_addr;
    struct syscall_regs* child_regs = (struct syscall_regs*)(ctx_addr + sizeof(struct context));

    *child_regs = *regs; // Copy user registers
    memset(child_ctx, 0, sizeof(struct context));
    child_ctx->rip = (uint64_t)fork_child_trampoline;

    child_thread->context = child_ctx;
    child_thread->rsp = (uint64_t)child_ctx;
    cpu_t* cpu = get_cpu();
    child_thread->saved_user_rsp = cpu->user_rsp; // Inherit user stack pointer


#ifdef TEST_MODE
    if (syscall_exit_hook)
    {
        // Capture a snapshot of the child stack layout before it ever runs.
        const uint64_t ctx_addr = (uint64_t)child_ctx;
        const uint64_t regs_addr = (uint64_t)child_regs;
        const uint64_t frame_words[4] = {
            *((uint64_t*)ctx_addr),
            *((uint64_t*)(ctx_addr + 8)),
            *((uint64_t*)(ctx_addr + 16)),
            *((uint64_t*)(ctx_addr + 24)),
        };
        printk("sys_fork: child pid=%d tid=%d rsp=0x%lx ktop=0x%lx saved_user_rsp=0x%lx\n",
               child_proc->pid,
               child_thread->tid,
               child_thread->rsp,
               child_thread->kstack_top,
               child_thread->saved_user_rsp);
        printk("sys_fork: child regs @0x%lx rcx=0x%lx r11=0x%lx\n",
               regs_addr,
               child_regs->rcx,
               child_regs->r11);
        printk("sys_fork: child ctx @0x%lx rip=0x%lx first_qwords=[%lx %lx %lx %lx]\n",
               ctx_addr,
               child_ctx->rip,
               frame_words[0],
               frame_words[1],
               frame_words[2],
               frame_words[3]);
    }
#endif

    TEST_SYSCALL_LOG("sys_fork: parent pid=%d child pid=%d\n", current_process->pid, child_proc->pid);

    return child_proc->pid;
}
