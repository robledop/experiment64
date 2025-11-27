#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdarg.h>
#include "limine.h"

#define TERMINAL_MARGIN 0
#define LINE_SPACING 5
#define FONT_HEIGHT 8

// Do not change this
typedef enum warning_level
{
    INFO,
    WARNING,
    ERROR,
} t;

void terminal_init(struct limine_framebuffer *fb);
void terminal_putc(char c);
void terminal_write(const char *data, size_t size);
void terminal_write_string(const char *data);
void vprintk(const char *format, va_list args);
void printk(const char *format, ...);
void terminal_set_cursor(int x, int y);
void terminal_get_cursor(int *x, int *y);
void terminal_get_dimensions(int *cols, int *rows);
void terminal_get_resolution(int *width, int *height);
void terminal_set_color(uint32_t color);
void terminal_clear(uint32_t color);
void terminal_scroll(int rows);
void boot_message(t level, const char *fmt, ...);

#ifdef TEST_MODE
void test_capture_begin(void);
void test_capture_discard(void);
void test_capture_flush(void);
#endif
