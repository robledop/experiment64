#include "test.h"
#include "cpu.h" 
#include "limine.h"

extern void *pmm_alloc_page(void);
extern void pmm_free_page(void *ptr);

TEST(test_pmm_alloc_free)
{
    void *page1 = pmm_alloc_page();
    ASSERT(page1 != NULL);

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
