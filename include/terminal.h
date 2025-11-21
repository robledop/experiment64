#pragma once

#include <stddef.h>
#include <stdint.h>
#include "limine.h"

void terminal_init(struct limine_framebuffer *fb);
void terminal_putc(char c);
void terminal_write(const char *data, size_t size);
void terminal_write_string(const char *data);
void printf(const char *format, ...);
void terminal_set_cursor(int x, int y);
void terminal_set_color(uint32_t color);
void terminal_clear(uint32_t color);
