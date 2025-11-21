#pragma once

#include <stddef.h>
#include <stdint.h>

void heap_init(uint64_t hhdm_offset);
void *kmalloc(size_t size);
void kfree(void *ptr);
void *kzalloc(size_t size);
void *krealloc(void *ptr, size_t new_size);
