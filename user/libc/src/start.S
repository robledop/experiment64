.intel_syntax noprefix
.global _start
.extern main
.extern exit

_start:
    # Stack is already set up by kernel
    call main
    mov rdi, rax
    call exit
    # Should not return
    hlt
.section .note.GNU-stack,"",@progbits
