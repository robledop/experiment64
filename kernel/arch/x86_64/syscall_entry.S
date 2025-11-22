.intel_syntax noprefix
.section .text
.global syscall_entry
.extern syscall_handler

syscall_entry:
    swapgs
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

.global fork_return
fork_return:
    # Child process starts here
    # RAX should be 0 for child
    xor rax, rax
    jmp syscall_return

.section .note.GNU-stack,"",@progbits
