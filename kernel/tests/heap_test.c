#include "test.h"
#include "heap.h"
#include "string.h"

TEST(test_kmalloc_small)
{
    void *ptr = kmalloc(16);
    ASSERT(ptr != NULL);
    memset(ptr, 0xAA, 16);
    kfree(ptr);
    return true;
}

TEST(test_kmalloc_large)
{
    // Larger than slab max (2048)
    void *ptr = kmalloc(4096);
    ASSERT(ptr != NULL);
    memset(ptr, 0xBB, 4096);
    kfree(ptr);
    return true;
}

TEST(test_kzalloc)
{
    void *ptr = kzalloc(64);
    ASSERT(ptr != NULL);
    char *c = (char *)ptr;
    for (int i = 0; i < 64; i++)
    {
        ASSERT(c[i] == 0);
    }
    kfree(ptr);
    return true;
}

TEST(test_krealloc)
{
    char *ptr = kmalloc(10);
    ASSERT(ptr != NULL);
    strcpy(ptr, "hello");

    ptr = krealloc(ptr, 20);
    ASSERT(ptr != NULL);
    ASSERT(strcmp(ptr, "hello") == 0);

    kfree(ptr);
    return true;
}

TEST(test_krealloc_shrink_in_place)
{
    char *ptr = kmalloc(64);
    ASSERT(ptr != NULL);
    memset(ptr, 0xCD, 64);

    char *same = krealloc(ptr, 32);
    ASSERT(same == ptr); // should reuse existing slab slot
    for (int i = 0; i < 32; i++)
    {
        ASSERT(same[i] == (char)0xCD);
    }

    kfree(same);
    return true;
}

TEST(test_krealloc_grow_crossing_slab_limit)
{
    char *ptr = kmalloc(128);
    ASSERT(ptr != NULL);
    strcpy(ptr, "grow-me");

    char *bigger = krealloc(ptr, 4096); // forces big allocation path
    ASSERT(bigger != NULL);
    ASSERT(strcmp(bigger, "grow-me") == 0);

    kfree(bigger);
    return true;
}

TEST(test_kmalloc_reuses_freed_slot)
{
    // Use smallest slab size to exercise free list reuse ordering.
    void *first = kmalloc(16);
    void *second = kmalloc(16);
    ASSERT(first != NULL && second != NULL && first != second);

    kfree(first);
    void *third = kmalloc(16);
    ASSERT(third == first); // recently freed slot should be first out

    kfree(second);
    kfree(third);
    return true;
}
