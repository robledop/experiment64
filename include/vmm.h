#pragma once

#include <stdint.h>
#include <stddef.h>

#define PAGE_SIZE 4096

#define PTE_PRESENT (1ull << 0)
#define PTE_WRITABLE (1ull << 1)
#define PTE_USER (1ull << 2)
#define PTE_HUGE (1ull << 7)
#define PTE_NX (1ull << 63)

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
