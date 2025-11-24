#include "test.h"
#include "vmm.h"
#include "limine.h"
#include "string.h"

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

    // Fill source page so we can verify the clone gets a deep copy.
    memset((void *)((uint64_t)phys + g_hhdm_offset), 0xA5, PAGE_SIZE);

    vmm_map_page(original, virt, (uint64_t)phys, PTE_PRESENT | PTE_WRITABLE);

    pml4_t clone = vmm_copy_pml4(original);
    ASSERT(clone != NULL);

    uint64_t resolved_clone = vmm_virt_to_phys(clone, virt);
    ASSERT(resolved_clone != 0);
    ASSERT(resolved_clone != (uint64_t)phys); // deep copy uses a new page

    uint8_t *orig_ptr = (uint8_t *)((uint64_t)phys + g_hhdm_offset);
    uint8_t *clone_ptr = (uint8_t *)(resolved_clone + g_hhdm_offset);
    for (int i = 0; i < 16; i++)
    {
        ASSERT(clone_ptr[i] == 0xA5);
    }

    // Mutate clone copy; original should remain unchanged.
    clone_ptr[0] = 0x3C;
    ASSERT(orig_ptr[0] == 0xA5);

    // Unmap in the clone only; original should stay mapped.
    vmm_unmap_page(clone, virt);
    ASSERT(vmm_virt_to_phys(clone, virt) == 0);
    ASSERT(vmm_virt_to_phys(original, virt) == (uint64_t)phys);

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

TEST(test_vmm_remap_overwrites_translation)
{
    pml4_t pml4 = vmm_new_pml4();
    ASSERT(pml4 != NULL);

    uint64_t virt = 0x500000000; // 20GB
    void *phys1 = pmm_alloc_page();
    void *phys2 = pmm_alloc_page();
    ASSERT(phys1 != NULL && phys2 != NULL && phys1 != phys2);

    vmm_map_page(pml4, virt, (uint64_t)phys1, PTE_PRESENT | PTE_WRITABLE);
    ASSERT(vmm_virt_to_phys(pml4, virt) == (uint64_t)phys1);

    // Remap same virtual address to a new physical page.
    vmm_map_page(pml4, virt, (uint64_t)phys2, PTE_PRESENT | PTE_WRITABLE);
    ASSERT(vmm_virt_to_phys(pml4, virt) == (uint64_t)phys2);

    vmm_destroy_pml4(pml4);
    return true;
}
