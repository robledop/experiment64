#include <tests/test.h>
#include <mem/dma.h>
#include <mem/pmm.h>
#include <mem/vmm.h>

TEST(test_dma_alloc_pages)
{
    constexpr size_t bytes = 256;
    uintptr_t phys         = 0;
    void *virt             = nullptr;

    TEST_ASSERT(dma_alloc_pages(bytes, 0, 0, &phys, &virt));
    TEST_ASSERT(phys != 0);
    TEST_ASSERT(virt != nullptr);
    TEST_ASSERT((phys & (PAGE_SIZE - 1u)) == 0);
    TEST_ASSERT((uintptr_t)virt == phys + g_hhdm_offset);

    const uint8_t *buf = (const uint8_t *)virt;
    for (size_t i = 0; i < bytes; i++) {
        TEST_ASSERT(buf[i] == 0);
    }

    dma_free_pages(phys, bytes);

    constexpr size_t aligned_bytes = 8192;
    constexpr size_t alignment     = 65536;
    constexpr size_t boundary      = 65536;
    uintptr_t aligned_phys         = 0;
    void *aligned_virt             = nullptr;

    TEST_ASSERT(dma_alloc_pages(aligned_bytes, alignment, boundary, &aligned_phys, &aligned_virt));
    TEST_ASSERT(aligned_phys != 0);
    TEST_ASSERT(aligned_virt != nullptr);
    TEST_ASSERT((aligned_phys & (alignment - 1u)) == 0);
    TEST_ASSERT((uintptr_t)aligned_virt == aligned_phys + g_hhdm_offset);
    TEST_ASSERT(((aligned_phys + aligned_bytes - 1u) & ~(boundary - 1u)) ==
                (aligned_phys & ~(boundary - 1u)));

    const uint8_t *aligned_buf = (const uint8_t *)aligned_virt;
    for (size_t i = 0; i < aligned_bytes; i++) {
        TEST_ASSERT(aligned_buf[i] == 0);
    }

    dma_free_pages(aligned_phys, aligned_bytes);
    return true;
}

TEST(test_dma_alloc_multipage)
{
    // Allocate more than one page to test multi-page path.
    constexpr size_t bytes = PAGE_SIZE * 3;
    uintptr_t phys = 0;
    void *virt = nullptr;

    TEST_ASSERT(dma_alloc_pages(bytes, 0, 0, &phys, &virt));
    TEST_ASSERT(phys != 0);
    TEST_ASSERT(virt != nullptr);
    TEST_ASSERT((phys & (PAGE_SIZE - 1u)) == 0);

    // Verify all pages are zero-filled.
    const uint8_t *buf = (const uint8_t *)virt;
    for (size_t i = 0; i < bytes; i += PAGE_SIZE)
        TEST_ASSERT(buf[i] == 0);

    dma_free_pages(phys, bytes);
    return true;
}

TEST(test_dma_alloc_large_alignment)
{
    // Test a large alignment requirement (1 MiB).
    constexpr size_t alignment = 1024 * 1024;
    constexpr size_t bytes = PAGE_SIZE;
    uintptr_t phys = 0;
    void *virt = nullptr;

    TEST_ASSERT(dma_alloc_pages(bytes, alignment, 0, &phys, &virt));
    TEST_ASSERT((phys & (alignment - 1u)) == 0);

    dma_free_pages(phys, bytes);
    return true;
}
