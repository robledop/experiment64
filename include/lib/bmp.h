#pragma once

#include <stdint.h>
typedef struct
{
    uint16_t bfType;      // must be "BM" (0x4D42)
    uint32_t bfSize;      // size of the file in bytes
    uint16_t bfReserved1; // reserved; must be 0
    uint16_t bfReserved2; // reserved; must be 0
    uint32_t bfOffBits;   // offset to start of pixel data
} __attribute__((packed)) BITMAPFILEHEADER;

typedef struct
{
    uint32_t biSize;         // size of this header (40 bytes)
    int32_t biWidth;         // width of the bitmap in pixels
    int32_t biHeight;        // height of the bitmap in pixels
    uint16_t biPlanes;       // number of color planes, must be 1
    uint16_t biBitCount;     // bits per pixel (1, 4, 8, 16, 24, 32)
    uint32_t biCompression;  // compression method (0 = BI_RGB for none)
    uint32_t biSizeImage;    // size of raw pixel data (can be 0 if BI_RGB)
    int32_t biXPelsPerMeter; // horizontal resolution (pixels per meter)
    int32_t biYPelsPerMeter; // vertical resolution (pixels per meter)
    uint32_t biClrUsed;      // number of colors in the palette
    uint32_t biClrImportant; // number of important colors
} __attribute__((packed)) BITMAPINFOHEADER;

// Loads a BMP file via the kernel VFS and converts it to 0xAARRGGBB pixels.
// Returns 0 on success; negative value on failure.
int bitmap_load_argb(const char *path, uint32_t **out_pixels, uint32_t *out_width, uint32_t *out_height);
