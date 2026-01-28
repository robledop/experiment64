#pragma once

#include <stddef.h>
#include <stdint.h>

bool dma_alloc_pages(size_t bytes,
                     size_t alignment,
                     size_t boundary,
                     uintptr_t *phys_out,
                     void **virt_out);
void dma_free_pages(uintptr_t addr, size_t bytes);
