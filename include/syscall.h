#pragma once

#include <stdint.h>

struct syscall_regs
{
    uint64_t rdi, rsi, rdx, r10, r8, r9;
    uint64_t r15, r14, r13, r12, rbx, rbp;
    uint64_t rcx, r11;
};

#define SYS_WRITE 0
#define SYS_READ 1
#define SYS_EXEC 2
#define SYS_EXIT 3
#define SYS_FORK 4
#define SYS_WAIT 5
#define SYS_GETPID 6
#define SYS_YIELD 7
#define SYS_SPAWN 8
#define SYS_SBRK 9
#define SYS_OPEN 10
#define SYS_CLOSE 11
#define SYS_READDIR 12
#define SYS_CHDIR 13
#define SYS_SLEEP 14
#define SYS_MKNOD 15
#define SYS_IOCTL 16
#define SYS_MMAP 17
#define SYS_MUNMAP 18
#define SYS_EXECVE 19
#define SYS_STAT 20
#define SYS_FSTAT 21
#define SYS_LINK 22
#define SYS_UNLINK 23
#define SYS_GETCWD 24
#define SYS_GETTIMEOFDAY 25
#define SYS_USLEEP 26

void syscall_init(void);
void syscall_set_exit_hook(void (*hook)(int));
void syscall_set_stack(uint64_t stack_top);

#ifdef TEST_MODE
extern volatile uint64_t test_syscall_count;
extern volatile uint64_t test_syscall_last_num;
extern volatile uint64_t test_syscall_last_arg1;
#endif
