#pragma once

#include <stdint.h>
#include "limine.h"

void framebuffer_init(struct limine_framebuffer *fb);
struct limine_framebuffer *framebuffer_current(void);

void framebuffer_fill_span32(uint32_t y, uint32_t x, uint32_t length, uint32_t color);
void framebuffer_copy_span32(uint32_t dst_y, uint32_t dst_x, uint32_t src_y, uint32_t src_x, uint32_t length);
void framebuffer_fill_rect32(uint32_t x, uint32_t y, uint32_t width, uint32_t height, uint32_t color);
void framebuffer_blit_span32(uint32_t y, uint32_t x, const uint32_t *src, uint32_t length);
void framebuffer_putpixel(uint32_t x, uint32_t y, uint32_t color);
void framebuffer_put_bitmap_32(uint32_t x, uint32_t y, const uint32_t *pixels, uint32_t width, uint32_t height);
