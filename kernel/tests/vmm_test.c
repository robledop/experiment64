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

TEST(test_vmm_copy_preserves_user_mapping)
{
    pml4_t original = vmm_new_pml4();
    ASSERT(original != NULL);

    uint64_t virt = 0x400000000; // 16GB
    void *phys = pmm_alloc_page();
    ASSERT(phys != NULL);

    vmm_map_page(original, virt, (uint64_t)phys, PTE_PRESENT | PTE_WRITABLE);

    pml4_t clone = vmm_copy_pml4(original);
    ASSERT(clone != NULL);

    uint64_t resolved = vmm_virt_to_phys(clone, virt);
    ASSERT(resolved != 0);

    vmm_destroy_pml4(clone);
    vmm_destroy_pml4(original);
    return true;
}

TEST(test_vmm_unmap_clears_translation)
{
    pml4_t pml4 = vmm_new_pml4();
    ASSERT(pml4 != NULL);

    uint64_t virt = 0x300000000; // 12GB
    void *phys = pmm_alloc_page();
    ASSERT(phys != NULL);

    vmm_map_page(pml4, virt, (uint64_t)phys, PTE_PRESENT | PTE_WRITABLE);
    ASSERT(vmm_virt_to_phys(pml4, virt) == ((uint64_t)phys));

    vmm_unmap_page(pml4, virt);
    ASSERT(vmm_virt_to_phys(pml4, virt) == 0);

    vmm_destroy_pml4(pml4);
    return true;
}
