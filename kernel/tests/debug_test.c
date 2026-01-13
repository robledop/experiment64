#include <tests/test.h>
#include <debug.h>
#include <arch/x86_64/cpu.h>

TEST_PRIO(test_panic_trap_path, 200)
{
    uint64_t rflags;
    __asm__ volatile("pushfq; pop %0" : "=r"(rflags));

    panic_trap_disable();
    panic_trap_expect();
    panic("panic trap test");

    const bool hit = panic_trap_triggered();
    panic_trap_disable();

    if (rflags & RFLAGS_IF)
        __asm__ volatile("sti" ::: "memory");

    TEST_ASSERT(hit);
    return true;
}
