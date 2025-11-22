#pragma once

#include <stdint.h>

void syscall_init(void);

#ifdef TEST_MODE
extern volatile uint64_t test_syscall_count;
extern volatile uint64_t test_syscall_last_num;
extern volatile uint64_t test_syscall_last_arg1;
#endif
