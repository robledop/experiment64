.altmacro

.macro ISR_NOERRCODE num
    .global isr\num
    isr\num:
        cli
        push 0
        push \num
        jmp isr_common_stub
.endm

.macro ISR_ERRCODE num
    .global isr\num
    isr\num:
        cli
        push \num
        jmp isr_common_stub
.endm

.macro ISR_HANDLER num
    // Exceptions with error codes: 8, 10-14, 17, 30
    .if \num == 8 || (\num >= 10 && \num <= 14) || \num == 17 || \num == 30
        ISR_ERRCODE \num
    .else
        ISR_NOERRCODE \num
    .endif
.endm

// Generate ISRs 0-255
.set i, 0
.rept 256
    ISR_HANDLER %i
    .set i, i + 1
.endr

isr_common_stub:
    push rax
    push rbx
    push rcx
    push rdx
    push rsi
    push rdi
    push rbp
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15

    mov rdi, rsp
    call exception_handler

    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rbp
    pop rdi
    pop rsi
    pop rdx
    pop rcx
    pop rbx
    pop rax
    add rsp, 16
    iretq

.macro ISR_ENTRY num
    .quad isr\num
.endm

.global isr_stub_table
isr_stub_table:
    .set i, 0
    .rept 256
        ISR_ENTRY %i
        .set i, i + 1
    .endr