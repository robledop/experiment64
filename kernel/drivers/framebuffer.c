#include "framebuffer.h"
#include <stddef.h>
#include <stdbool.h>
#include <string.h>

#include "assert.h"
#include "test.h"
#include "devfs.h"
#include "vfs.h"
#include "ioctl.h"

static struct limine_framebuffer* active_fb = nullptr;

static inline uint32_t framebuffer_width(void)
{
    assert(active_fb != nullptr);
    return (uint32_t)active_fb->width;
}

static inline uint32_t framebuffer_height(void)
{
    assert(active_fb != nullptr);
    return (uint32_t)active_fb->height;
}

static inline uint32_t* framebuffer_row(uint32_t y)
{
    assert(active_fb != nullptr);
    return (uint32_t*)((uint8_t*)active_fb->address + (uint64_t)y * active_fb->pitch);
}

static uint64_t framebuffer_size_bytes(const struct limine_framebuffer* fb)
{
    if (!fb)
        return 0;
    return fb->pitch * fb->height;
}

static uint64_t framebuffer_dev_read(const vfs_inode_t* node, uint64_t offset, uint64_t size, uint8_t* buffer)
{
    struct limine_framebuffer* fb = node ? (struct limine_framebuffer*)node->device : nullptr;
    if (!fb)
        fb = framebuffer_current();
    if (!fb)
        return 0;

    uint64_t fb_size = framebuffer_size_bytes(fb);
    if (offset >= fb_size)
        return 0;

    uint64_t to_copy = size;
    if (offset + to_copy > fb_size)
        to_copy = fb_size - offset;

    memcpy(buffer, (uint8_t*)fb->address + offset, to_copy);
    return to_copy;
}

static uint64_t framebuffer_dev_write(vfs_inode_t* node, uint64_t offset, uint64_t size, uint8_t* buffer)
{
    struct limine_framebuffer* fb = node ? (struct limine_framebuffer*)node->device : nullptr;
    if (!fb)
        fb = framebuffer_current();
    if (!fb)
        return 0;

    uint64_t fb_size = framebuffer_size_bytes(fb);
    if (offset >= fb_size)
        return 0;

    uint64_t to_copy = size;
    if (offset + to_copy > fb_size)
        to_copy = fb_size - offset;

    memcpy((uint8_t*)fb->address + offset, buffer, to_copy);
    return to_copy;
}

static int framebuffer_dev_ioctl(vfs_inode_t* node, int request, void* arg)
{
    struct limine_framebuffer* fb = node ? (struct limine_framebuffer*)node->device : nullptr;
    if (!fb)
        fb = framebuffer_current();
    if (!fb)
        return -1;

    switch (request)
    {
    case FB_IOCTL_GET_WIDTH:
        if (!arg)
            return -1;
        *(uint32_t*)arg = (uint32_t)fb->width;
        return 0;
    case FB_IOCTL_GET_HEIGHT:
        if (!arg)
            return -1;
        *(uint32_t*)arg = (uint32_t)fb->height;
        return 0;
    case FB_IOCTL_GET_PITCH:
        if (!arg)
            return -1;
        *(uint32_t*)arg = fb->pitch;
        return 0;
    case FB_IOCTL_GET_FBADDR:
        if (!arg)
            return -1;
        *(uint64_t*)arg = (uint64_t)fb->address;
        return 0;
    default:
        return -1;
    }
}

static struct inode_operations framebuffer_dev_ops = {
    .read = framebuffer_dev_read,
    .write = framebuffer_dev_write,
    .ioctl = framebuffer_dev_ioctl,
};

static vfs_inode_t framebuffer_device_node;
static bool framebuffer_device_registered = false;

void framebuffer_init(struct limine_framebuffer* fb)
{
    assert(fb != nullptr);
    active_fb = fb;

    framebuffer_device_node.flags = VFS_CHARDEVICE;
    framebuffer_device_node.iops = &framebuffer_dev_ops;
    framebuffer_device_node.size = framebuffer_size_bytes(fb);
    framebuffer_device_node.device = fb;

    if (!framebuffer_device_registered)
    {
        devfs_register_device("fb0", &framebuffer_device_node);
        framebuffer_device_registered = true;
    }
}

struct limine_framebuffer* framebuffer_current(void)
{
    return active_fb;
}

void framebuffer_fill_span32(uint32_t y, uint32_t x, uint32_t length, uint32_t color)
{
    assert(active_fb != nullptr);

    if (!active_fb || length == 0 || y >= framebuffer_height() || x >= framebuffer_width())
        return;

    uint32_t width = framebuffer_width();
    if (x + length > width)
        length = width - x;

    uint32_t* row = framebuffer_row(y);
    if (!row)
        return;

    for (uint32_t i = 0; i < length; i++)
        row[x + i] = color;
}

void framebuffer_copy_span32(uint32_t dst_y, uint32_t dst_x, uint32_t src_y, uint32_t src_x, uint32_t length)
{
    assert(active_fb != nullptr);

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

    uint32_t* dst = framebuffer_row(dst_y);
    uint32_t* src = framebuffer_row(src_y);
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
    assert(active_fb != nullptr);

    if (!active_fb || width == 0 || height == 0)
        return;

    const uint32_t fb_width = framebuffer_width();
    const uint32_t fb_height = framebuffer_height();

    if (x >= fb_width || y >= fb_height)
        return;

    if (x + width > fb_width)
        width = fb_width - x;
    if (y + height > fb_height)
        height = fb_height - y;

    for (uint32_t row = 0; row < height; row++)
        framebuffer_fill_span32(y + row, x, width, color);
}

void framebuffer_blit_span32(uint32_t y, uint32_t x, const uint32_t* src, uint32_t length)
{
    assert(active_fb != nullptr);

    if (!active_fb || !src || length == 0 || y >= framebuffer_height() || x >= framebuffer_width())
        return;

    uint32_t width = framebuffer_width();
    if (x + length > width)
        length = width - x;

    uint32_t* row = framebuffer_row(y);
    if (!row)
        return;

    for (uint32_t i = 0; i < length; i++)
        row[x + i] = src[i];
}

void framebuffer_putpixel(uint32_t x, uint32_t y, uint32_t color)
{
    framebuffer_fill_span32(y, x, 1, color);
}

void framebuffer_put_bitmap_32(uint32_t x, uint32_t y, const uint32_t* pixels, uint32_t width, uint32_t height)
{
    assert(active_fb != nullptr);

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
