#include "debug.h"
#include <stdint.h>

// https://wiki.osdev.org/Stack_Smashing_Protector
// __stack_chk_guard is initialized in kernel_main

uintptr_t __stack_chk_guard = 0x595e9fbd94fda766;

[[noreturn]] void __stack_chk_fail(void) // NOLINT(*-reserved-identifier)
{
    panic("Stack smashing detected");

    __builtin_unreachable();
}
