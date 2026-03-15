#include <tests/test.h>
#include <arch/x86_64/cpu.h>
#include <mem/pmm.h>
#include <mem/vmm.h>

TEST(test_pmm_alloc_free)
{
    void *page1 = pmm_alloc_page();
    TEST_ASSERT(page1 != nullptr);
    TEST_ASSERT(((uintptr_t)page1 & (PAGE_SIZE - 1)) == 0); // aligned

    void *page2 = pmm_alloc_page();
    TEST_ASSERT(page2 != nullptr);
    TEST_ASSERT(page1 != page2);

    pmm_free_page(page1);
    pmm_free_page(page2);

    // Ideally, we would check if they are free, but that's hard without exposing internals.
    // Re-allocating might give back the same pages (LIFO/Stack) or different (Bitmap scan).
    // For now just checking it doesn't crash is good.
    return true;
}

TEST(test_pmm_alloc_pages_contiguous)
{
    // Request three contiguous pages and verify they are actually contiguous.
    void *block = pmm_alloc_pages(3);
    TEST_ASSERT(block != nullptr);

    const uintptr_t base = (uintptr_t)block;
    TEST_ASSERT((base & (PAGE_SIZE - 1)) == 0); // aligned

    // Allocate an individual page and verify it doesn't overlap with our contiguous block.
    void *single = pmm_alloc_page();
    TEST_ASSERT(single != nullptr);
    uintptr_t single_phys = (uintptr_t)single;
    TEST_ASSERT(single_phys < base || single_phys >= base + 3 * PAGE_SIZE);
    pmm_free_page(single);

    // Write a unique pattern to each page via HHDM and verify no aliasing.
    uint8_t *page0 = (uint8_t *)(base + g_hhdm_offset);
    uint8_t *page1 = (uint8_t *)(base + PAGE_SIZE + g_hhdm_offset);
    uint8_t *page2 = (uint8_t *)(base + 2 * PAGE_SIZE + g_hhdm_offset);

    page0[0] = 0xAA;
    page1[0] = 0xBB;
    page2[0] = 0xCC;

    TEST_ASSERT(page0[0] == 0xAA);
    TEST_ASSERT(page1[0] == 0xBB);
    TEST_ASSERT(page2[0] == 0xCC);

    pmm_free_pages(block, 3);

    // Allocate again and ensure we still get something valid.
    void *block2 = pmm_alloc_pages(3);
    TEST_ASSERT(block2 != nullptr);
    pmm_free_pages(block2, 3);
    return true;
}

TEST(test_pmm_large_contiguous_alignment)
{
    // Grab a larger run to ensure alignment and contiguity assumptions scale.
    constexpr size_t pages = 17;
    void *block = pmm_alloc_pages(pages);
    TEST_ASSERT(block != nullptr);
    const uintptr_t base = (uintptr_t)block;
    TEST_ASSERT((base & (PAGE_SIZE - 1)) == 0);

    // Check the first and last page addresses line up as expected.
    const uintptr_t last = base + (pages - 1) * PAGE_SIZE;
    TEST_ASSERT(((last) & (PAGE_SIZE - 1)) == 0);

    pmm_free_pages(block, pages);
    return true;
}

TEST(test_pmm_reserves_bitmap_guard)
{
    size_t reserved_page = pmm_get_reserved_base_page();
    TEST_ASSERT(reserved_page > 0);

    uint64_t guard_phys = reserved_page * PAGE_SIZE;
    uint64_t bitmap_phys = pmm_get_bitmap_phys();
    size_t bitmap_pages = (pmm_get_bitmap_size() + PAGE_SIZE - 1) / PAGE_SIZE;
    uint64_t bitmap_end = bitmap_phys + bitmap_pages * PAGE_SIZE;

    // Single-page allocations must not dip into the bitmap or its guard.
    void *pages[8] = {nullptr};
    for (size_t i = 0; i < 8; i++)
    {
        pages[i] = pmm_alloc_page();
        TEST_ASSERT(pages[i] != nullptr);
        uintptr_t phys = (uintptr_t)pages[i];
        TEST_ASSERT(phys >= guard_phys);
        TEST_ASSERT(!(phys >= bitmap_phys && phys < bitmap_end));
    }

    for (size_t i = 0; i < 8; i++)
    {
        pmm_free_page(pages[i]);
    }

    // Contiguous allocations should also respect the guard.
    constexpr size_t block_pages = 5;
    void *block = pmm_alloc_pages(block_pages);
    TEST_ASSERT(block != nullptr);
    uintptr_t block_phys = (uintptr_t)block;
    TEST_ASSERT(block_phys >= guard_phys);
    TEST_ASSERT(!(block_phys >= bitmap_phys && block_phys < bitmap_end));
    TEST_ASSERT(!((block_phys + block_pages * PAGE_SIZE) > bitmap_phys && block_phys < bitmap_end));

    pmm_free_pages(block, block_pages);
    return true;
}
