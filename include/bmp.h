#pragma once

#include <types.h>
typedef struct {
    u16 bfType;      // must be "BM" (0x4D42)
    u32 bfSize;      // size of the file in bytes
    u16 bfReserved1; // reserved; must be 0
    u16 bfReserved2; // reserved; must be 0
    u32 bfOffBits;   // offset to start of pixel data
} __attribute__((packed)) BITMAPFILEHEADER;

typedef struct {
    u32 biSize;         // size of this header (40 bytes)
    i32 biWidth;         // width of the bitmap in pixels
    i32 biHeight;        // height of the bitmap in pixels
    u16 biPlanes;       // number of color planes, must be 1
    u16 biBitCount;     // bits per pixel (1, 4, 8, 16, 24, 32)
    u32 biCompression;  // compression method (0 = BI_RGB for none)
    u32 biSizeImage;    // size of raw pixel data (can be 0 if BI_RGB)
    i32 biXPelsPerMeter; // horizontal resolution (pixels per meter)
    i32 biYPelsPerMeter; // vertical resolution (pixels per meter)
    u32 biClrUsed;      // number of colors in the palette
    u32 biClrImportant; // number of important colors
} __attribute__((packed)) BITMAPINFOHEADER;

int bitmap_load_argb(const char *path, u32 **out_pixels);
