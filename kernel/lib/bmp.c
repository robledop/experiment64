#include <stdint.h>
#include "bmp.h"
#include "heap.h"
#include "terminal.h"
#include "vfs.h"
#include "kasan.h"

enum
{
    BI_RGB = 0,
    BI_BITFIELDS = 3
};

/**
 * @brief Load a BMP image from disk into an ARGB buffer.
 *
 * Converts 24-bit BMP files to a 32-bit ARGB pixel array allocated via the
 * kernel heap. Ownership of the buffer transfers to the caller.
 *
 * @param path Filesystem path to the BMP asset.
 * @param[out] out_pixels Receives the newly allocated ARGB buffer.
 * @param[out] out_width Receives the bitmap width (optional).
 * @param[out] out_height Receives the bitmap height (optional).
 * @return 0 on success, negative value on failure.
 */
int bitmap_load_argb(const char *path, uint32_t **out_pixels, uint32_t *out_width, uint32_t *out_height)
{
    if (!path || !out_pixels)
    {
        return -1;
    }

    vfs_inode_t *node = vfs_resolve_path(path);
    if (!node)
    {
        printk("BMP: Failed to resolve %s\n", path);
        return -1;
    }

    uint64_t file_size = node->size;
    if (file_size < sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER))
    {
        printk("BMP: File too small %s\n", path);
        return -1;
    }

    uint8_t *buffer = kmalloc(file_size);
    if (!buffer)
    {
        printk("BMP: Out of memory\n");
        return -1;
    }
#ifdef KASAN
    if (kasan_is_ready() && kasan_shadow_value(buffer) != KASAN_POISON_ACCESSIBLE)
        kasan_unpoison_range(buffer, file_size);
#endif

    if (vfs_read(node, 0, file_size, buffer) != file_size)
    {
        printk("BMP: Failed to read %s\n", path);
        kfree(buffer);
        return -1;
    }

    BITMAPFILEHEADER *fh = (BITMAPFILEHEADER *)buffer;
    BITMAPINFOHEADER *ih = (BITMAPINFOHEADER *)(buffer + sizeof(BITMAPFILEHEADER));

    if (fh->bfType != 0x4D42 || ih->biPlanes != 1)
    {
        printk("BMP: Invalid header for %s\n", path);
        kfree(buffer);
        return -1;
    }

    if (!(ih->biBitCount == 24 && (ih->biCompression == BI_RGB || ih->biCompression == BI_BITFIELDS)))
    {
        printk("BMP: Unsupported format (%d bpp)\n", ih->biBitCount);
        kfree(buffer);
        return -1;
    }

    int width = ih->biWidth;
    int height = (ih->biHeight > 0) ? ih->biHeight : -ih->biHeight;
    int top_down = (ih->biHeight < 0);

    if (width <= 0 || height <= 0)
    {
        printk("BMP: Invalid dimensions (%d x %d)\n", width, height);
        kfree(buffer);
        return -1;
    }

    uint32_t *pixels = kmalloc(sizeof(uint32_t) * (uint64_t)width * (uint64_t)height);
    if (!pixels)
    {
        printk("BMP: Out of memory for pixels\n");
        kfree(buffer);
        return -1;
    }

    const int bytes_per_row = ((width * 3 + 3) / 4) * 4; // padded to 4-byte boundary

    for (int y = 0; y < height; y++)
    {
        uint8_t *row = buffer + fh->bfOffBits + y * bytes_per_row;
        int dest_y = top_down ? y : (height - 1 - y);

        for (int x = 0; x < width; x++)
        {
            uint8_t b = row[x * 3 + 0];
            uint8_t g = row[x * 3 + 1];
            uint8_t r = row[x * 3 + 2];
            pixels[dest_y * width + x] = (0xFFu << 24) | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
        }
    }

    kfree(buffer);

    *out_pixels = pixels;
    if (out_width)
    {
        *out_width = (uint32_t)width;
    }
    if (out_height)
    {
        *out_height = (uint32_t)height;
    }

    return 0;
}
