#include "terminal.h"
#include "font.h"
#include "uart.h"
#include <stdarg.h>

static struct limine_framebuffer *terminal_fb = NULL;
static int terminal_x = 0;
static int terminal_y = 0;
static uint32_t terminal_color = 0xFFFFFFFF;
static uint32_t terminal_bg_color = 0x00000000;

#define LINE_SPACING 5

static void terminal_draw_cursor(int x, int y, uint32_t color)
{
    if (!terminal_fb)
        return;
    for (int row = 6; row < 8; row++)
    {
        for (int col = 0; col < 8; col++)
        {
            uint64_t offset = (y + row) * terminal_fb->pitch + (x + col) * 4;
            if (offset < terminal_fb->pitch * terminal_fb->height)
            {
                uint32_t *pixel = (uint32_t *)((uint8_t *)terminal_fb->address + offset);
                *pixel = color;
            }
        }
    }
}

void terminal_init(struct limine_framebuffer *fb)
{
    terminal_fb = fb;
    terminal_x = 0;
    terminal_y = 0;
    terminal_bg_color = 0x00000000;
}

void terminal_set_cursor(int x, int y)
{
    if (terminal_fb)
        terminal_draw_cursor(terminal_x, terminal_y, terminal_bg_color);
    terminal_x = x;
    terminal_y = y;
    if (terminal_fb)
        terminal_draw_cursor(terminal_x, terminal_y, terminal_color);
}

void terminal_set_color(uint32_t color)
{
    terminal_color = color;
}

void terminal_clear(uint32_t color)
{
    terminal_bg_color = color;
    if (!terminal_fb)
        return;
    for (size_t y = 0; y < terminal_fb->height; y++)
    {
        uint32_t *fb_ptr = (uint32_t *)((uint8_t *)terminal_fb->address + y * terminal_fb->pitch);
        for (size_t x = 0; x < terminal_fb->width; x++)
        {
            fb_ptr[x] = color;
        }
    }
    terminal_draw_cursor(terminal_x, terminal_y, terminal_color);
}

void terminal_putc(char c)
{
    uart_putc(c);
    if (!terminal_fb)
        return;

    terminal_draw_cursor(terminal_x, terminal_y, terminal_bg_color);

    if (c == '\n')
    {
        terminal_x = 0;
        terminal_y += 8 + LINE_SPACING;
        terminal_draw_cursor(terminal_x, terminal_y, terminal_color);
        return;
    }

    if (c < 32 || c > 126)
        c = '?';

    const uint8_t *glyph = font8x8_basic[c - 32];

    for (int row = 0; row < 8; row++)
    {
        for (int col = 0; col < 8; col++)
        {
            if ((glyph[row] >> (7 - col)) & 1)
            {
                uint64_t offset = (terminal_y + row) * terminal_fb->pitch + (terminal_x + col) * 4;
                if (offset < terminal_fb->pitch * terminal_fb->height)
                {
                    uint32_t *pixel = (uint32_t *)((uint8_t *)terminal_fb->address + offset);
                    *pixel = terminal_color;
                }
            }
        }
    }
    terminal_x += 8;
    if (terminal_x >= (int)terminal_fb->width)
    {
        terminal_x = 0;
        terminal_y += 8 + LINE_SPACING;
    }
    terminal_draw_cursor(terminal_x, terminal_y, terminal_color);
}

void terminal_write(const char *data, size_t size)
{
    for (size_t i = 0; i < size; i++)
        terminal_putc(data[i]);
}

void terminal_write_string(const char *data)
{
    while (*data)
        terminal_putc(*data++);
}

void printf(const char *format, ...)
{
    va_list args;
    va_start(args, format);

    while (*format)
    {
        if (*format == '%')
        {
            format++;

            // Handle length modifiers
            int is_long = 0;
            if (*format == 'l')
            {
                is_long = 1;
                format++;
                if (*format == 'l')
                {
                    is_long = 2; // Treat long long same as long for now (64-bit)
                    format++;
                }
            }

            if (*format == 's')
            {
                const char *s = va_arg(args, const char *);
                terminal_write_string(s ? s : "(null)");
            }
            else if (*format == 'd' || *format == 'i')
            {
                long d;
                if (is_long)
                    d = va_arg(args, long);
                else
                    d = va_arg(args, int);

                char buf[32];
                int i = 0;
                bool neg = d < 0;
                unsigned long u = neg ? -d : d;

                if (u == 0)
                    buf[i++] = '0';
                while (u)
                {
                    buf[i++] = (u % 10) + '0';
                    u /= 10;
                }
                if (neg)
                    buf[i++] = '-';
                while (i > 0)
                    terminal_putc(buf[--i]);
            }
            else if (*format == 'u')
            {
                unsigned long u;
                if (is_long)
                    u = va_arg(args, unsigned long);
                else
                    u = va_arg(args, unsigned int);

                char buf[32];
                int i = 0;
                if (u == 0)
                    buf[i++] = '0';
                while (u)
                {
                    buf[i++] = (u % 10) + '0';
                    u /= 10;
                }
                while (i > 0)
                    terminal_putc(buf[--i]);
            }
            else if (*format == 'x' || *format == 'X' || *format == 'p')
            {
                unsigned long x;
                if (is_long || *format == 'p')
                    x = va_arg(args, unsigned long);
                else
                    x = va_arg(args, unsigned int);

                char buf[32];
                int i = 0;
                if (x == 0)
                    buf[i++] = '0';
                while (x)
                {
                    int digit = x % 16;
                    buf[i++] = digit < 10 ? digit + '0' : digit - 10 + 'a';
                    x /= 16;
                }
                while (i > 0)
                    terminal_putc(buf[--i]);
            }
            else if (*format == 'c')
            {
                char c = (char)va_arg(args, int);
                terminal_putc(c);
            }
            else if (*format == '%')
            {
                terminal_putc('%');
            }
        }
        else
        {
            terminal_putc(*format);
        }
        format++;
    }

    va_end(args);
}
