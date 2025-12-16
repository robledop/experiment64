#include "test.h"
#include "process.h"

static void dummy_entry(void)
{
    // Never scheduled in this test.
    while (1)
        __asm__ volatile("hlt");
}

TEST_PRIO(test_kstack_has_syscall_headroom, 50)
{
    process_t *p = process_create("kstack_test");
    TEST_ASSERT(p != nullptr);

    thread_t *t = thread_create(p, dummy_entry, false);
    TEST_ASSERT(t != nullptr);

    const uintptr_t top = (uintptr_t)t->kstack_top;
    const uintptr_t ctx = (uintptr_t)t->context;

    // Must leave enough room above the context-switch frame so syscall_entry pushes
    // won't clobber it (currently ~112 bytes worth of pushes).
    const size_t slack = (size_t)(top - ctx);
    TEST_ASSERT(slack >= (512 + sizeof(struct context)));

    process_destroy(p);
    return true;
}


