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
