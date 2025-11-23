.global switch_to
.type switch_to, @function

/*
 * void switch_to(thread_t *prev, thread_t *next);
 * RDI = prev
 * RSI = next
 *
 * struct Thread {
 *     int tid;             // 0
 *     // padding 4 bytes   // 4
 *     process_t *process;  // 8
 *     uint64_t rsp;        // 16
 *     ...
 * }
 */
switch_to:
    /* Save callee-saved registers */
    push rbx
    push rbp
    push r12
    push r13
    push r14
    push r15

    /* Save RSP to prev->rsp (offset 16) */
    mov [rdi + 16], rsp

    /* Load RSP from next->rsp (offset 16) */
    mov rsp, [rsi + 16]

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
    # Release scheduler lock (held by previous thread)
    # void spinlock_release(spinlock_t *lock);
    # RDI = &scheduler_lock
    lea rdi, [rip + scheduler_lock]
    call spinlock_release

    sti
    call r12
    
    # If entry returns, call sys_exit(0)
    mov rdi, 0
    call sys_exit
    hlt
.section .note.GNU-stack,"",@progbits
