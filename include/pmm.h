#pragma once

#include <stdint.h>
#include <stddef.h>

#define PAGE_SIZE 4096

void pmm_init(uint64_t hhdm_offset);
void *pmm_alloc_page(void);
void pmm_free_page(void *ptr);
void *pmm_alloc_pages(size_t count);
void pmm_free_pages(void *ptr, size_t count);
