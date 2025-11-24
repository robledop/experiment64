#ifdef KASAN
#include "test.h"
#include "kasan.h"
#include "heap.h"
#include <stdbool.h>
#include "string.h"

TEST_PRIO(test_kasan_basic_access, 5)
{
    // Shadow should be initialized before tests run.
    ASSERT(kasan_is_ready());

    void *ptr = kmalloc(64);
    ASSERT(ptr != NULL);

    // Should be considered accessible.
    ASSERT(kasan_check_range(ptr, 64, true, __builtin_return_address(0)));

    kfree(ptr);
    return true;
}

TEST_PRIO(test_kasan_poison_cycle, 6)
{
    ASSERT(kasan_is_ready());
    void *ptr = kmalloc(64);
    ASSERT(ptr != NULL);

    uint8_t shadow_before_free = kasan_shadow_value(ptr);
    ASSERT(shadow_before_free == 0x00);

    kfree(ptr);

    uint8_t shadow_after_free = kasan_shadow_value(ptr);
    ASSERT(shadow_after_free == 0xFF);

    return true;
}

TEST_PRIO(test_kasan_detect_overflow, 7)
{
    ASSERT(kasan_is_ready());
    void *ptr = kmalloc(16);
    ASSERT(ptr != NULL);

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
    ASSERT(kasan_is_ready());
    void *ptr = kmalloc(32);
    ASSERT(ptr != NULL);
    kfree(ptr);

    uint8_t shadow = kasan_shadow_value(ptr);
    return shadow == 0xFF;
}
#else
typedef int kasan_test_dummy;
#endif
