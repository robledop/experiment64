#include "test.h"
#include "heap.h"
#include "string.h"
#include "pmm.h"

TEST(test_kmalloc_small)
{
    void *ptr = kmalloc(16);
    TEST_ASSERT(ptr != nullptr);
    TEST_ASSERT(((uintptr_t)ptr & (sizeof(void *) - 1)) == 0); // at least pointer aligned
    memset(ptr, 0xAA, 16);
    kfree(ptr);
    return true;
}

TEST(test_kmalloc_large)
{
    // Larger than slab max (2048)
    void *ptr = kmalloc(4096);
    TEST_ASSERT(ptr != nullptr);
    TEST_ASSERT(((uintptr_t)ptr & (sizeof(void *) - 1)) == 0); // at least word aligned
    memset(ptr, 0xBB, 4096);
    kfree(ptr);
    return true;
}

TEST(test_kmalloc_zero_returns_null)
{
    TEST_ASSERT(kmalloc(0) == nullptr);
    return true;
}

TEST(test_kfree_null_noop)
{
    kfree(nullptr);
    return true;
}

TEST(test_kzalloc)
{
    void *ptr = kzalloc(64);
    TEST_ASSERT(ptr != nullptr);
    char *c = (char *)ptr;
    for (int i = 0; i < 64; i++)
    {
        TEST_ASSERT(c[i] == 0);
    }
    kfree(ptr);
    return true;
}

TEST(test_kzalloc_fragmentation_resilience)
{
    // Interleave small and large allocations and ensure zero-fill holds.
    void *small1 = kzalloc(32);
    void *large = kzalloc(4096);
    void *small2 = kzalloc(48);
    TEST_ASSERT(small1 && large && small2);

    for (int i = 0; i < 32; i++)
        TEST_ASSERT(((char *)small1)[i] == 0);
    for (int i = 0; i < 48; i++)
        TEST_ASSERT(((char *)small2)[i] == 0);

    memset(large, 0x5A, 4096);
    kfree(small1);
    kfree(large);
    kfree(small2);
    return true;
}

TEST(test_krealloc)
{
    char *ptr = kmalloc(10);
    TEST_ASSERT(ptr != nullptr);
    strcpy(ptr, "hello");

    ptr = krealloc(ptr, 20);
    TEST_ASSERT(ptr != nullptr);
    TEST_ASSERT(strcmp(ptr, "hello") == 0);

    kfree(ptr);
    return true;
}

TEST(test_krealloc_to_zero_frees)
{
    void *ptr = kmalloc(64);
    TEST_ASSERT(ptr != nullptr);
    memset(ptr, 0xAB, 64);

    void *res = krealloc(ptr, 0);
    TEST_ASSERT(res == nullptr); // should free and return nullptr
    return true;
}

TEST(test_krealloc_null_allocates)
{
    char *ptr = krealloc(nullptr, 32);
    TEST_ASSERT(ptr != nullptr);
    memset(ptr, 0xEF, 32);
    kfree(ptr);
    return true;
}

TEST(test_krealloc_shrink_in_place)
{
    char *ptr = kmalloc(64);
    TEST_ASSERT(ptr != nullptr);
    memset(ptr, 0xCD, 64);

    char *same = krealloc(ptr, 32);
    TEST_ASSERT(same == ptr); // should reuse existing slab slot
    for (int i = 0; i < 32; i++)
    {
        TEST_ASSERT(same[i] == (char)0xCD);
    }

    kfree(same);
    return true;
}

TEST(test_krealloc_grow_crossing_slab_limit)
{
    char *ptr = kmalloc(128);
    TEST_ASSERT(ptr != nullptr);
    strcpy(ptr, "grow-me");

    char *bigger = krealloc(ptr, 4096); // forces big allocation path
    TEST_ASSERT(bigger != nullptr);
    TEST_ASSERT(strcmp(bigger, "grow-me") == 0);

    kfree(bigger);
    return true;
}

TEST(test_kmalloc_reuses_freed_slot)
{
    // Use smallest slab size to exercise free list reuse ordering.
    void *first = kmalloc(16);
    void *second = kmalloc(16);
    TEST_ASSERT(first != nullptr && second != nullptr && first != second);

    kfree(first);
    void *third = kmalloc(16);
    TEST_ASSERT(third == first); // recently freed slot should be first out

    kfree(second);
    kfree(third);
    return true;
}
