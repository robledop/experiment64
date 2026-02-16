.intel_syntax noprefix
.global _start
.extern main
.extern exit
.extern __tls_init

_start:
    # Stack layout: argc, argv..., nullptr, envp..., nullptr
    mov rdi, [rsp]        # argc
    lea rsi, [rsp + 8]    # argv
    xor rdx, rdx          # envp (currently unused, set to nullptr)
    and rsp, -16          # align stack to 16-byte boundary as per SysV ABI
    # Save argc/argv across TLS init
    push rsi
    push rdi
    call __tls_init
    pop rdi
    pop rsi
    call main
    mov rdi, rax
    call exit
    # Should not return
    hlt
.section .note.GNU-stack,"",@progbits
