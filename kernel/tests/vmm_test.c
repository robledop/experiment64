#include "test.h"
#include "vmm.h"
#include "limine.h"

extern void *pmm_alloc_page(void);

TEST(test_vmm_map)
{
    pml4_t pml4 = vmm_new_pml4();
    ASSERT(pml4 != NULL);

    uint64_t virt = 0x200000000; // 8GB
    void *phys = pmm_alloc_page();

    vmm_map_page(pml4, virt, (uint64_t)phys, PTE_PRESENT | PTE_WRITABLE);

    return true;
}
