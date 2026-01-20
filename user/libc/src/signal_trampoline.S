.intel_syntax noprefix
#include <sys/syscall.h>

.global __signal_trampoline
__signal_trampoline:
    mov rdi, rsp
    mov rax, SYS_SIGRETURN
    syscall
    hlt

.section .note.GNU-stack,"",@progbits
