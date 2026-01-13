#pragma once

#include <stdint.h>
#include <stddef.h>
#include <lib/ansi.h>

#if !defined(TEST_MODE)
[[noreturn]]
#endif
void panic(const char *fmt, ...);
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
