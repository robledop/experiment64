#pragma once

#include <stdint.h>
#include <stddef.h>

#define PAGE_SIZE 4096

void pmm_init(uint64_t hhdm_offset);
void *pmm_alloc_page(void);
void pmm_free_page(void *ptr);
void *pmm_alloc_pages(size_t count);
void pmm_free_pages(void *ptr, size_t count);
uint64_t pmm_get_highest_addr(void);
size_t pmm_get_reserved_base_page(void);
uint64_t pmm_get_bitmap_phys(void);
size_t pmm_get_bitmap_size(void);
/** Count currently-free physical pages (scans the bitmap; for tests/diagnostics). */
size_t pmm_count_free_pages(void);
