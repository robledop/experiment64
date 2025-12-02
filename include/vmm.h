#pragma once

#include <stdint.h>
#include <stddef.h>
#include "pmm.h" // for PAGE_SIZE

#define PTE_PRESENT (1ull << 0)
#define PTE_WRITABLE (1ull << 1)
#define PTE_USER (1ull << 2)
#define PTE_PWT (1ull << 3) // Page Write-Through
#define PTE_PCD (1ull << 4) // Page Cache Disable
#define PTE_HUGE (1ull << 7)
#define PTE_PAT (1ull << 7)       // PAT bit for 4KB pages (same bit as HUGE, but different meaning)
#define PTE_PAT_HUGE (1ull << 12) // PAT bit for 2MB/1GB pages
#define PTE_NX (1ull << 63)

// Combined flags for Write-Combining memory (using PAT index 1 = WC)
// PAT index is formed by: PAT bit (bit 7) | PCD (bit 4) | PWT (bit 3)
// Default PAT: index 0=WB, 1=WT, 2=UC-, 3=UC, 4=WB, 5=WT, 6=UC-, 7=UC
// We'll reprogram PAT so index 1 = WC: PAT=0, PCD=0, PWT=1
#define PTE_WRITE_COMBINING PTE_PWT

typedef uint64_t *pml4_t;

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
void vmm_remap_wc(uint64_t virt_start, uint64_t size);
