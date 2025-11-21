#include "vmm.h"
#include "pmm.h"
#include "string.h"
#include <stdint.h>

static uint64_t g_hhdm_offset = 0;

void vmm_init(uint64_t hhdm_offset)
{
    g_hhdm_offset = hhdm_offset;
}

static uint64_t *get_next_level(uint64_t *current_level, size_t index, bool allocate)
{
    if (current_level[index] & PTE_PRESENT)
    {
        uint64_t phys = current_level[index] & 0x000FFFFFFFFFF000;
        return (uint64_t *)(phys + g_hhdm_offset);
    }

    if (!allocate)
    {
        return NULL;
    }

    void *phys = pmm_alloc_page();
    if (!phys)
    {
        return NULL;
    }

    uint64_t *virt = (uint64_t *)((uint64_t)phys + g_hhdm_offset);
    memset(virt, 0, PAGE_SIZE);

    // Present, Writable, User (allow access to children)
    current_level[index] = (uint64_t)phys | PTE_PRESENT | PTE_WRITABLE | PTE_USER;
    return virt;
}

void vmm_map_page(pml4_t pml4, uint64_t virt, uint64_t phys, uint64_t flags)
{
    size_t pml4_idx = (virt >> 39) & 0x1FF;
    size_t pdpt_idx = (virt >> 30) & 0x1FF;
    size_t pd_idx = (virt >> 21) & 0x1FF;
    size_t pt_idx = (virt >> 12) & 0x1FF;

    uint64_t *pml4_virt = (uint64_t *)((uint64_t)pml4 + g_hhdm_offset);

    uint64_t *pdpt_virt = get_next_level(pml4_virt, pml4_idx, true);
    if (!pdpt_virt)
        return;

    uint64_t *pd_virt = get_next_level(pdpt_virt, pdpt_idx, true);
    if (!pd_virt)
        return;

    uint64_t *pt_virt = get_next_level(pd_virt, pd_idx, true);
    if (!pt_virt)
        return;

    pt_virt[pt_idx] = phys | flags;

    // Invalidate TLB if we are modifying the current address space?
    // For now, we assume we are building a new one or mapping new pages.
    // If mapping into current, we should invlpg.
    __asm__ volatile("invlpg (%0)" : : "r"(virt) : "memory");
}

void vmm_unmap_page(pml4_t pml4, uint64_t virt)
{
    // Similar traversal, but clear the entry.
    // We won't free the page tables for now to keep it simple.
    size_t pml4_idx = (virt >> 39) & 0x1FF;
    size_t pdpt_idx = (virt >> 30) & 0x1FF;
    size_t pd_idx = (virt >> 21) & 0x1FF;
    size_t pt_idx = (virt >> 12) & 0x1FF;

    uint64_t *pml4_virt = (uint64_t *)((uint64_t)pml4 + g_hhdm_offset);

    uint64_t *pdpt_virt = get_next_level(pml4_virt, pml4_idx, false);
    if (!pdpt_virt)
        return;

    uint64_t *pd_virt = get_next_level(pdpt_virt, pdpt_idx, false);
    if (!pd_virt)
        return;

    uint64_t *pt_virt = get_next_level(pd_virt, pd_idx, false);
    if (!pt_virt)
        return;

    pt_virt[pt_idx] = 0;
    __asm__ volatile("invlpg (%0)" : : "r"(virt) : "memory");
}

pml4_t vmm_new_pml4(void)
{
    void *phys = pmm_alloc_page();
    if (!phys)
        return NULL;

    uint64_t *virt = (uint64_t *)((uint64_t)phys + g_hhdm_offset);
    memset(virt, 0, PAGE_SIZE);

    // We might want to map the kernel into this new PML4.
    // Limine provides the kernel mapping in the boot PML4.
    // We should copy the higher half mappings from the current PML4 (bootloader's).

    // Get current CR3
    uint64_t current_cr3;
    __asm__ volatile("mov %%cr3, %0" : "=r"(current_cr3));
    uint64_t current_pml4_phys = current_cr3 & 0x000FFFFFFFFFF000;
    uint64_t *current_pml4_virt = (uint64_t *)(current_pml4_phys + g_hhdm_offset);

    // Copy higher half (entries 256-511)
    for (int i = 256; i < 512; i++)
    {
        virt[i] = current_pml4_virt[i];
    }

    return (pml4_t)phys;
}

void vmm_switch_pml4(pml4_t pml4)
{
    __asm__ volatile("mov %0, %%cr3" : : "r"(pml4) : "memory");
}

uint64_t vmm_virt_to_phys(pml4_t pml4, uint64_t virt)
{
    size_t pml4_idx = (virt >> 39) & 0x1FF;
    size_t pdpt_idx = (virt >> 30) & 0x1FF;
    size_t pd_idx = (virt >> 21) & 0x1FF;
    size_t pt_idx = (virt >> 12) & 0x1FF;

    uint64_t *pml4_virt = (uint64_t *)((uint64_t)pml4 + g_hhdm_offset);

    uint64_t *pdpt_virt = get_next_level(pml4_virt, pml4_idx, false);
    if (!pdpt_virt)
        return 0;

    uint64_t *pd_virt = get_next_level(pdpt_virt, pdpt_idx, false);
    if (!pd_virt)
        return 0;

    uint64_t *pt_virt = get_next_level(pd_virt, pd_idx, false);
    if (!pt_virt)
        return 0;

    if (!(pt_virt[pt_idx] & PTE_PRESENT))
        return 0;

    return (pt_virt[pt_idx] & 0x000FFFFFFFFFF000) + (virt & 0xFFF);
}
