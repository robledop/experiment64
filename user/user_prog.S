.intel_syntax noprefix
.global _start
_start:
    mov rax, 0      # sys_write
    mov rdi, 1      # stdout
    lea rsi, [rip + msg]
    mov rdx, 12
    syscall

    mov rax, 3      # sys_exit
    mov rdi, 123    # exit code
    syscall

msg: .ascii "Hello Exec!\n"
.section .note.GNU-stack,"",@progbits
