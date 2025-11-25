#pragma once

#include <stdint.h>
#include <stdarg.h>
#include <stdbool.h>

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

#if !defined(TEST_MODE)
[[noreturn]]
#endif
void panic(const char* fmt, ...);
void stack_trace(void);
void debug_init(void);
int panic_trap_setjmp(void);
void panic_trap_expect(void);
void panic_trap_disable(void);
bool panic_trap_triggered(void);
bool panic_trap_active(void);
void panic_trap_mark_hit(void);

// Stack Smashing Protector
extern uintptr_t __stack_chk_guard;
[[noreturn]] void __stack_chk_fail(void);
