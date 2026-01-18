#pragma once

#include <stddef.h>
#include <stdint.h>

void heap_init(uint64_t hhdm_offset);
void *kmalloc(size_t size);
void kfree(void *ptr);
void *kzalloc(size_t size);
void *krealloc(void *ptr, size_t new_size);
// Debug: check if a physical page backs a slab
bool heap_is_slab_page(const void *phys);
bool heap_is_slab_range(const void *virt_ptr, size_t len);
bool heap_is_slab_header_range(const void *virt_ptr, size_t len);
