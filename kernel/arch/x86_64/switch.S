.intel_syntax noprefix
.global switch_to
.type switch_to, @function

/*
 * void switch_to(thread_t *prev, thread_t *next);
 * RDI = prev
 * RSI = next
 *
 * struct Thread {
 *     fpu_state_t fpu_state; // 0
 *     process_t *process;    // 1024
 *     uint64_t rsp;          // 1032
 *     ...
 * }
 *
 * THREAD_RSP_OFFSET is checked at compile time by a static_assert next to the
 * thread_t definition in include/task/process.h; if the struct layout changes,
 * the build fails there rather than corrupting context switches at runtime.
 */
#define THREAD_RSP_OFFSET 1032
switch_to:
    /* Save callee-saved registers */
    push rbx
    push rbp
    push r12
    push r13
    push r14
    push r15

    /* Save RSP to prev->rsp */
    mov [rdi + THREAD_RSP_OFFSET], rsp

    /* Load RSP from next->rsp */
    mov rsp, [rsi + THREAD_RSP_OFFSET]

    /* Restore callee-saved registers */
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbp
    pop rbx

    ret

.global thread_trampoline
thread_trampoline:
    # New threads start execution with interrupts disabled (inherited from the
    # scheduler thread / schedule() path). Enable interrupts before calling the
    # C entrypoint.
    sti
    call r12
    
    # If entry returns, call sys_exit(0)
    mov rdi, 0
    call sys_exit
    hlt
.section .note.GNU-stack,"",@progbits
