#include "test.h"
#include "bmp.h"
#include "vfs.h"
#include "heap.h"
#include "string.h"

TEST(test_bmp_load_valid)
{
    // Create a 2x2 BMP
    // File Header (14) + Info Header (40) + Pixel Data (8 * 2 = 16) = 70 bytes
    uint8_t bmp_data[70];
    memset(bmp_data, 0, sizeof(bmp_data));

    BITMAPFILEHEADER *fh = (BITMAPFILEHEADER *)bmp_data;
    fh->bfType = 0x4D42; // 'BM'
    fh->bfSize = 70;
    fh->bfOffBits = 54;

    BITMAPINFOHEADER *ih = (BITMAPINFOHEADER *)(bmp_data + 14);
    ih->biSize = 40;
    ih->biWidth = 2;
    ih->biHeight = 2; // Bottom-up (positive height)
    ih->biPlanes = 1;
    ih->biBitCount = 24;
    ih->biCompression = 0; // BI_RGB
    ih->biSizeImage = 16;

    // Pixel Data at offset 54
    // Row 0 (Bottom of image): Red (00 00 FF), Red (00 00 FF), Padding (00 00)
    uint8_t *pixels = bmp_data + 54;
    // Pixel 0,0 (Bottom-Left)
    pixels[0] = 0x00; // B
    pixels[1] = 0x00; // G
    pixels[2] = 0xFF; // R
    // Pixel 1,0 (Bottom-Right)
    pixels[3] = 0x00;
    pixels[4] = 0x00;
    pixels[5] = 0xFF;
    // Padding
    pixels[6] = 0x00;
    pixels[7] = 0x00;

    // Row 1 (Top of image): Blue (FF 00 00), Blue (FF 00 00), Padding
    uint8_t *row1 = pixels + 8;
    // Pixel 0,1 (Top-Left)
    row1[0] = 0xFF; // B
    row1[1] = 0x00; // G
    row1[2] = 0x00; // R
    // Pixel 1,1 (Top-Right)
    row1[3] = 0xFF;
    row1[4] = 0x00;
    row1[5] = 0x00;
    // Padding
    row1[6] = 0x00;
    row1[7] = 0x00;

    // Write to file
    const char *filename = "test.bmp";
    vfs_mknod((char *)filename, VFS_FILE, 0);
    vfs_inode_t *node = vfs_resolve_path(filename);

    ASSERT(node != NULL);

    // Truncate/Overwrite
    ASSERT(vfs_write(node, 0, sizeof(bmp_data), bmp_data) == sizeof(bmp_data));
    vfs_close(node);

    // Load it back
    uint32_t *out_pixels = NULL;
    uint32_t w = 0, h = 0;
    int result = bitmap_load_argb(filename, &out_pixels, &w, &h);

    ASSERT(result == 0);
    ASSERT(out_pixels != NULL);
    ASSERT(w == 2);
    ASSERT(h == 2);

    // Verify pixels
    // The loader converts bottom-up BMP to top-down buffer.
    // File Row 0 (Red) -> Image Row 1 (Bottom).
    // File Row 1 (Blue) -> Image Row 0 (Top).

    // Image Row 0 (Top) should be Blue.
    // Blue in file: B=FF, G=00, R=00.
    // ARGB: FF 00 00 FF. (A R G B) -> 0xFF0000FF.

    // Image Row 1 (Bottom) should be Red.
    // Red in file: B=00, G=00, R=FF.
    // ARGB: FF FF 00 00. -> 0xFFFF0000.

    // Pixel at (0,0) (Top-Left) -> Index 0. Should be Blue.
    ASSERT(out_pixels[0] == 0xFF0000FF);
    // Pixel at (0,1) (Bottom-Left) -> Index 2. Should be Red.
    ASSERT(out_pixels[2] == 0xFFFF0000);

    kfree(out_pixels);
    return true;
}

TEST(test_bmp_invalid_file)
{
    uint32_t *p;
    uint32_t w, h;
    ASSERT(bitmap_load_argb("nonexistent_bmp_file.bmp", &p, &w, &h) != 0);
    return true;
}

TEST(test_bmp_bad_header)
{
    // Create a file with bad magic
    uint8_t bad_data[100];
    memset(bad_data, 0, sizeof(bad_data));
    // Magic is 00 00

    const char *filename = "bad.bmp";
    vfs_mknod((char *)filename, VFS_FILE, 0);
    vfs_inode_t *node = vfs_resolve_path(filename);
    ASSERT(node != NULL);
    vfs_write(node, 0, sizeof(bad_data), bad_data);
    vfs_close(node);

    uint32_t *p;
    uint32_t w, h;
    ASSERT(bitmap_load_argb(filename, &p, &w, &h) != 0);
    return true;
}
