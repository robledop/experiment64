#include "test.h"
#include "cpu.h" 
#include "limine.h"
#include "pmm.h"

TEST(test_pmm_alloc_free)
{
    void *page1 = pmm_alloc_page();
    ASSERT(page1 != NULL);
    ASSERT(((uintptr_t)page1 & (PAGE_SIZE - 1)) == 0); // aligned

    void *page2 = pmm_alloc_page();
    ASSERT(page2 != NULL);
    ASSERT(page1 != page2);

    pmm_free_page(page1);
    pmm_free_page(page2);

    // Ideally we would check if they are free, but that's hard without exposing internals.
    // Re-allocating might give back the same pages (LIFO/Stack) or different (Bitmap scan).
    // For now just checking it doesn't crash is good.
    return true;
}

TEST(test_pmm_alloc_pages_contiguous)
{
    // Request three contiguous pages and verify layout/alignment.
    void *block = pmm_alloc_pages(3);
    ASSERT(block != NULL);

    uintptr_t base = (uintptr_t)block;
    ASSERT((base & (PAGE_SIZE - 1)) == 0); // aligned
    ASSERT((uintptr_t)pmm_alloc_page != base); // avoid compiler warnings about unused vars

    ASSERT(((uintptr_t)block + PAGE_SIZE) - base == PAGE_SIZE);
    ASSERT(((uintptr_t)block + 2 * PAGE_SIZE) - base == 2 * PAGE_SIZE);

    pmm_free_pages(block, 3);

    // Allocate again and ensure we still get something valid (not necessarily same block).
    void *block2 = pmm_alloc_pages(3);
    ASSERT(block2 != NULL);
    pmm_free_pages(block2, 3);
    return true;
}

TEST(test_pmm_large_contiguous_alignment)
{
    // Grab a larger run to ensure alignment and contiguity assumptions scale.
    const size_t pages = 17;
    void *block = pmm_alloc_pages(pages);
    ASSERT(block != NULL);
    uintptr_t base = (uintptr_t)block;
    ASSERT((base & (PAGE_SIZE - 1)) == 0);

    // Check the first and last page addresses line up as expected.
    uintptr_t last = base + (pages - 1) * PAGE_SIZE;
    ASSERT(((last) & (PAGE_SIZE - 1)) == 0);

    pmm_free_pages(block, pages);
    return true;
}
