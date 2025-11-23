#pragma once

#include <stdint.h>

// ANSI Color Codes
#define KNRM "\x1B[0m"
#define KRED "\x1B[31m"
#define KGRN "\x1B[32m"
#define KYEL "\x1B[33m"
#define KBLU "\x1B[34m"
#define KMAG "\x1B[35m"
#define KCYN "\x1B[36m"
#define KWHT "\x1B[37m"
#define KRESET "\x1B[0m"
#define KBWHT "\x1B[1;37m"

void panic(const char *fmt, ...);
void stack_trace(void);
void debug_init(void);

// Stack Smashing Protector
extern uintptr_t __stack_chk_guard;
[[noreturn]] void __stack_chk_fail(void);
