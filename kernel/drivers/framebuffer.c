#include "framebuffer.h"
#include <stddef.h>

static struct limine_framebuffer *active_fb = NULL;

static inline uint32_t framebuffer_width(void)
{
    return active_fb ? (uint32_t)active_fb->width : 0;
}

static inline uint32_t framebuffer_height(void)
{
    return active_fb ? (uint32_t)active_fb->height : 0;
}

static inline uint32_t *framebuffer_row(uint32_t y)
{
    if (!active_fb)
        return NULL;
    return (uint32_t *)((uint8_t *)active_fb->address + (uint64_t)y * active_fb->pitch);
}

void framebuffer_init(struct limine_framebuffer *fb)
{
    active_fb = fb;
}

struct limine_framebuffer *framebuffer_current(void)
{
    return active_fb;
}

void framebuffer_fill_span32(uint32_t y, uint32_t x, uint32_t length, uint32_t color)
{
    if (!active_fb || length == 0 || y >= framebuffer_height() || x >= framebuffer_width())
        return;

    uint32_t width = framebuffer_width();
    if (x + length > width)
        length = width - x;

    uint32_t *row = framebuffer_row(y);
    if (!row)
        return;

    for (uint32_t i = 0; i < length; i++)
        row[x + i] = color;
}

void framebuffer_copy_span32(uint32_t dst_y, uint32_t dst_x, uint32_t src_y, uint32_t src_x, uint32_t length)
{
    if (!active_fb || length == 0)
        return;

    uint32_t width = framebuffer_width();
    uint32_t height = framebuffer_height();
    if (dst_y >= height || src_y >= height || dst_x >= width || src_x >= width)
        return;

    uint32_t max_len = width - dst_x;
    if (length > max_len)
        length = max_len;
    max_len = width - src_x;
    if (length > max_len)
        length = max_len;

    uint32_t *dst = framebuffer_row(dst_y);
    uint32_t *src = framebuffer_row(src_y);
    if (!dst || !src)
        return;

    dst += dst_x;
    src += src_x;

    if (dst < src)
    {
        for (uint32_t i = 0; i < length; i++)
            dst[i] = src[i];
    }
    else if (dst > src)
    {
        for (uint32_t i = length; i > 0; i--)
            dst[i - 1] = src[i - 1];
    }
    else
    {
        // Same region, nothing to do.
    }
}

void framebuffer_fill_rect32(uint32_t x, uint32_t y, uint32_t width, uint32_t height, uint32_t color)
{
    if (!active_fb || width == 0 || height == 0)
        return;

    uint32_t fb_width = framebuffer_width();
    uint32_t fb_height = framebuffer_height();

    if (x >= fb_width || y >= fb_height)
        return;

    if (x + width > fb_width)
        width = fb_width - x;
    if (y + height > fb_height)
        height = fb_height - y;

    for (uint32_t row = 0; row < height; row++)
        framebuffer_fill_span32(y + row, x, width, color);
}

void framebuffer_blit_span32(uint32_t y, uint32_t x, const uint32_t *src, uint32_t length)
{
    if (!active_fb || !src || length == 0 || y >= framebuffer_height() || x >= framebuffer_width())
        return;

    uint32_t width = framebuffer_width();
    if (x + length > width)
        length = width - x;

    uint32_t *row = framebuffer_row(y);
    if (!row)
        return;

    for (uint32_t i = 0; i < length; i++)
        row[x + i] = src[i];
}

void framebuffer_putpixel(uint32_t x, uint32_t y, uint32_t color)
{
    framebuffer_fill_span32(y, x, 1, color);
}

void framebuffer_put_bitmap_32(uint32_t x, uint32_t y, const uint32_t *pixels, uint32_t width, uint32_t height)
{
    if (!active_fb || !pixels || width == 0 || height == 0)
        return;

    uint32_t fb_width = framebuffer_width();
    uint32_t fb_height = framebuffer_height();

    if (x >= fb_width || y >= fb_height)
        return;

    if (x + width > fb_width)
        width = fb_width - x;
    if (y + height > fb_height)
        height = fb_height - y;

    for (uint32_t row = 0; row < height; row++)
        framebuffer_blit_span32(y + row, x, pixels + row * width, width);
}
