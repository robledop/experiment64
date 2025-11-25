#ifdef KASAN
#include "test.h"
#include "kasan.h"
#include "heap.h"
#include <stdbool.h>
#include "string.h"
#include "debug.h"

TEST_PRIO(test_kasan_basic_access, 5)
{
    // Shadow should be initialized before tests run.
    TEST_ASSERT(kasan_is_ready());

    void *ptr = kmalloc(64);
    TEST_ASSERT(ptr != nullptr);

    // Should be considered accessible.
    TEST_ASSERT(kasan_check_range(ptr, 64, true, __builtin_return_address(0)));

    kfree(ptr);
    return true;
}

TEST_PRIO(test_kasan_poison_cycle, 6)
{
    TEST_ASSERT(kasan_is_ready());
    void *ptr = kmalloc(64);
    TEST_ASSERT(ptr != nullptr);

    uint8_t shadow_before_free = kasan_shadow_value(ptr);
    TEST_ASSERT(shadow_before_free == 0x00);

    kfree(ptr);

    uint8_t shadow_after_free = kasan_shadow_value(ptr);
    TEST_ASSERT(shadow_after_free == 0xFF);

    return true;
}

TEST_PRIO(test_kasan_detect_overflow, 7)
{
    TEST_ASSERT(kasan_is_ready());
    void *ptr = kmalloc(16);
    TEST_ASSERT(ptr != nullptr);

    // Find the first poisoned byte after the unpoisoned user area.
    uint8_t *bytes = (uint8_t *)ptr;
    size_t max_probe = 256; // plenty for small caches
    size_t first_poison = max_probe;
    for (size_t i = 0; i < max_probe; i++)
    {
        if (kasan_shadow_value(bytes + i) != 0x00)
        {
            first_poison = i;
            break;
        }
    }

    kfree(ptr);
    return first_poison < max_probe;
}

TEST_PRIO(test_kasan_detect_use_after_free, 8)
{
    TEST_ASSERT(kasan_is_ready());
    void *ptr = kmalloc(32);
    TEST_ASSERT(ptr != nullptr);
    kfree(ptr);

    uint8_t shadow = kasan_shadow_value(ptr);
    return shadow == 0xFF;
}

TEST_PRIO(test_kasan_trap_overflow_panics, 9)
{
    TEST_ASSERT(kasan_is_ready());
    panic_trap_setjmp();
    panic_trap_expect();
    uint8_t *p = kmalloc(8);
    TEST_ASSERT(p != nullptr);
    bool bad = !kasan_check_range(p + 8, 1, true, __builtin_return_address(0));
    bool hit = panic_trap_triggered();
    panic_trap_disable();
    return bad && hit;
}

TEST_PRIO(test_kasan_trap_use_after_free_panics, 10)
{
    TEST_ASSERT(kasan_is_ready());
    panic_trap_setjmp();
    panic_trap_expect();
    uint8_t *p = kmalloc(8);
    TEST_ASSERT(p != nullptr);
    kfree(p);
    bool bad = !kasan_check_range(p, 1, true, __builtin_return_address(0));
    bool hit = panic_trap_triggered();
    panic_trap_disable();
    return bad && hit;
}
#else
typedef int kasan_test_dummy;
#endif
