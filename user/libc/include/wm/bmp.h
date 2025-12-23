#pragma once

#include <stdint.h>

typedef struct {
    uint16_t bfType;
    uint32_t bfSize;
    uint16_t bfReserved1;
    uint16_t bfReserved2;
    uint32_t bfOffBits;
} __attribute__((packed)) BITMAPFILEHEADER;

typedef struct {
    uint32_t biSize;
    int32_t biWidth;
    int32_t biHeight;
    uint16_t biPlanes;
    uint16_t biBitCount;
    uint32_t biCompression;
    uint32_t biSizeImage;
    int32_t biXPelsPerMeter;
    int32_t biYPelsPerMeter;
    uint32_t biClrUsed;
    uint32_t biClrImportant;
} __attribute__((packed)) BITMAPINFOHEADER;

// Load a 24-bit BMP from disk and convert it to a tightly packed ARGB buffer.
// The returned buffer is width*height pixels, row-major, no padding.
// Caller owns *out_pixels and must free() it.
int bitmap_load_argb(const char *path, uint32_t **out_pixels, uint32_t *out_width, uint32_t *out_height);

