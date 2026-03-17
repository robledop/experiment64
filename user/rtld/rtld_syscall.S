#
# @file rtld_syscall.S
# @brief Raw syscall wrappers for the runtime linker.

# The runtime linker cannot use libc, so it needs its own syscall
# interface. These follow the x86_64 Linux/custom syscall convention:
#  rax = syscall number
#  rdi = arg1, rsi = arg2, rdx = arg3, r10 = arg4, r8 = arg5, r9 = arg6
#  return value in rax
#
.intel_syntax noprefix

.section .text

.hidden rtld_syscall0
.hidden rtld_syscall1
.hidden rtld_syscall2
.hidden rtld_syscall3
.hidden rtld_syscall6

.global rtld_syscall0
rtld_syscall0:
    mov rax, rdi
    syscall
    ret

.global rtld_syscall1
rtld_syscall1:
    mov rax, rdi
    mov rdi, rsi
    syscall
    ret

.global rtld_syscall2
rtld_syscall2:
    mov rax, rdi
    mov rdi, rsi
    mov rsi, rdx
    syscall
    ret

.global rtld_syscall3
rtld_syscall3:
    mov rax, rdi
    mov rdi, rsi
    mov rsi, rdx
    mov rdx, rcx
    syscall
    ret

.global rtld_syscall6
rtld_syscall6:
    mov rax, rdi
    mov rdi, rsi
    mov rsi, rdx
    mov rdx, rcx
    mov r10, r8
    mov r8, r9
    mov r9, [rsp + 8]
    syscall
    ret
