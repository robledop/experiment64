#pragma once

#include <stdint.h>

#define PTE_PRESENT (1ull << 0)  // Page Present
#define PTE_WRITABLE (1ull << 1) // Page Writeable
#define PTE_USER (1ull << 2)     // User Access
#define PTE_PWT (1ull << 3)      // Page Write-Through
#define PTE_PCD (1ull << 4)      // Page Cache Disable
#define PTE_HUGE (1ull << 7)     // Huge Page (1GB in PDPT, 2MB in PD)
#define PTE_NX (1ull << 63)      // No Execute

/**
 * PML4 is the top-level page table in x86_64. It contains 512 entries, each of which points to a PDPT (Page Directory
 * Pointer Table). Each entry is 8 bytes, so the total size of a PML4 is 512 * 8 = 4096 bytes (1 page). We can represent
 * it as a pointer to an array of 512 uint64_t entries, but since we will be allocating it as a page, we can just use a
 * pointer to uint64_t and treat it as an array of 512 entries.
 */
typedef uint64_t *pml4_t;

/**
 * The offset to the Higher Half Direct Map (HHDM). This is the offset that the kernel is mapped at in the higher half.
 * We need this to convert between physical and virtual addresses when manipulating page tables, since the page tables
 * themselves are allocated in physical memory, but we need to access them in the kernel's virtual address space. The
 * HHDM is a direct mapping of physical memory into the higher half of the virtual address space, so we can convert a
 * physical address to a virtual address by adding this offset and convert a virtual address to a physical address by
 * subtracting this offset. This is set during vmm_init() and should be the same as the offset provided by the
 * bootloader (Limine) for the kernel's higher half mapping.
 */
extern uint64_t g_hhdm_offset;

void vmm_init(uint64_t hhdm_offset);
void vmm_map_page(pml4_t pml4, uint64_t virt, uint64_t phys, uint64_t flags);
void vmm_unmap_page(pml4_t pml4, uint64_t virt);
pml4_t vmm_new_pml4(void);
pml4_t vmm_copy_pml4(pml4_t src);
void vmm_destroy_pml4(pml4_t pml4);
void vmm_switch_pml4(const uint64_t *pml4);
void vmm_finalize(void);
uint64_t vmm_virt_to_phys(pml4_t pml4, uint64_t virt);
