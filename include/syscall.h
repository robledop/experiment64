#pragma once

#include <stdint.h>

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

void syscall_init(void);
void syscall_set_exit_hook(void (*hook)(int));
void syscall_set_stack(uint64_t stack_top);

#ifdef TEST_MODE
extern volatile uint64_t test_syscall_count;
extern volatile uint64_t test_syscall_last_num;
extern volatile uint64_t test_syscall_last_arg1;
#endif
