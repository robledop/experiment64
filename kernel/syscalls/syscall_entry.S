/*
 * syscall_entry.S - the ring3->ring0 syscall trampoline (MSR_LSTAR target).
 *
 * The `syscall` instruction jumps straight here; LSTAR is pointed at
 * syscall_entry by syscall_init() in kernel/syscalls/syscall.c. On entry we
 * are still on the user stack with the user GS base, so we:
 *   1. swapgs to the kernel cpu_t (set up during bring-up, see cpu.h),
 *   2. stash the user RSP at gs:8 (cpu_t.user_rsp) and load the kernel RSP
 *      from gs:16 (cpu_t.kernel_rsp) -- hardcoded byte offsets into struct cpu
 *      (include/arch/x86_64/cpu.h); they must stay in sync with that layout.
 *
 * The register push block is the exact REVERSE of the field order in
 * struct syscall_regs (include/sys/syscall.h): r11 is pushed first and rdi
 * last, so after the pushes RSP points at a valid `struct syscall_regs *`
 * (rdi at the lowest address == first struct field). That pointer is handed
 * to syscall_handler in r8, letting fork/sigreturn/signal-delivery edit the
 * saved register frame in place.
 *
 * Returns leave through syscall_return, which pops the frame in struct order,
 * restores user RSP from gs:8, swapgs's back and sysretq's. Other paths
 * re-enter here too: fork's child resumes at fork_child_trampoline ->
 * fork_return -> syscall_return (kernel/syscalls/sys_fork.c builds the frame),
 * and signal return (kernel/syscalls/sys_sigreturn.c, kernel/task/signal.c)
 * rewrites the syscall_regs frame and falls out through syscall_return.
 */
.intel_syntax noprefix
.section .text
.global syscall_entry
.extern syscall_handler

syscall_entry:
    swapgs               # Switch to Kernel GS

    mov [gs:8], rsp      # Save User RSP to cpu->user_rsp
    mov rsp, [gs:16]     # Load Kernel RSP from cpu->kernel_rsp

    # Save registers (that we want to preserve)
    push r11 # RFLAGS
    push rcx # RIP
    push rbp
    push rbx
    push r12
    push r13
    push r14
    push r15
    
    # Save arguments
    push r9
    push r8
    push r10
    push rdx
    push rsi
    push rdi

    # Prepare arguments for C function
    # void syscall_handler(uint64_t num, uint64_t a1, uint64_t a2, uint64_t a3)
    # RDI = RAX (syscall number)
    # RSI = RDI (arg1)
    # RDX = RSI (arg2)
    # RCX = RDX (arg3)
    
    # Stack layout (top to bottom):
    # RDI, RSI, RDX, R10, R8, R9, R15, ...
    # [rsp]      = RDI
    # [rsp + 8]  = RSI
    # [rsp + 16] = RDX
    # [rsp + 24] = R10
    
    mov rcx, [rsp + 16] # RCX = old RDX (arg3)
    mov rdx, [rsp + 8]  # RDX = old RSI (arg2)
    mov rsi, [rsp]      # RSI = old RDI (arg1)
    mov rdi, rax        # RDI = RAX (syscall num)
    mov r8, rsp         # R8 = pointer to regs

    call syscall_handler

    # Disable interrupts before restoring user RSP and swapgs.
    cli

    # Restore registers
.global syscall_return
syscall_return:
    pop rdi
    pop rsi
    pop rdx
    pop r10
    pop r8
    pop r9
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbx
    pop rbp
    pop rcx
    pop r11
    
    # Restore User RSP
    mov rsp, [gs:8]
    swapgs
    
    sysretq

	.global fork_child_trampoline
	fork_child_trampoline:
	    # RSP points to syscall_regs.
	    # No sti here: syscall_return restores RFLAGS (including IF) from the
	    # child's saved r11 via sysretq, so enabling interrupts early would only
	    # open a window where an IRQ is taken mid-return with the user GS/RSP.
	    jmp fork_return

.global fork_return
fork_return:
    # Child process starts here
    # RAX should be 0 for child
    xor rax, rax
    jmp syscall_return

.section .note.GNU-stack,"",@progbits
