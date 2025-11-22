#include <wm/bmp.h>
#include <user.h>
#include <fcntl.h>
#include <stat.h>

enum { BI_RGB = 0, BI_BITFIELDS = 3 };

/**
 * @brief Load a BMP image from disk into an ARGB buffer.
 *
 * Converts 24-bit BMP files to a 32-bit ARGB pixel array allocated via the
 * kernel heap. Ownership of the buffer transfers to the caller.
 *
 * @param path Filesystem path to the BMP asset.
 * @param[out] out_pixels Receives the newly allocated ARGB buffer.
 * @return 0 on success, negative value on failure.
 */
__attribute__((target("avx,sse2")))
int bitmap_load_argb(const char *path, u32 **out_pixels)
{
    const int fd = open(path, O_RDONLY);
    if (fd == -1) {
        return -1;
    }

    struct stat file_stat;
    fstat(fd, &file_stat);

    if (file_stat.size < sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER)) {
        close(fd);
        return -1;
    }

    u8 *buffer = malloc(file_stat.size * sizeof(u8));

    read(fd, buffer, file_stat.size);
    close(fd);

    BITMAPFILEHEADER *fh = (BITMAPFILEHEADER *)buffer;
    BITMAPINFOHEADER ih  = *(BITMAPINFOHEADER *)(buffer + sizeof(BITMAPFILEHEADER));

    if (fh->bfType != 0x4D42) {
        panic("Not a BMP file\n");
    }

    if (ih.biPlanes != 1) {
        panic("Not a BMP file\n");
    }

    if (ih.biBitCount != 24) {
        panic("Unsupported BMP format\n");
    }

    if (!(ih.biCompression == BI_RGB || (ih.biCompression == BI_BITFIELDS && ih.biBitCount == 32))) {
        panic("Unsupported BMP format\n");
    }

    const int width    = ih.biWidth;
    const int height   = (ih.biHeight > 0) ? ih.biHeight : -ih.biHeight;
    const int top_down = (ih.biHeight < 0);

    *out_pixels = (u32 *)malloc(sizeof(u32) * width * height);

    if (ih.biBitCount == 24) {
        const int bytes_per_row = ((width * 3 + 3) / 4) * 4; // padded to 4

        for (int y = 0; y < height; y++) {
            u8 *row = buffer + fh->bfOffBits + y * bytes_per_row;

            int destY = top_down ? y : (height - 1 - y);
            for (int x = 0; x < width; x++) {
                u8 B                             = row[x * 3 + 0];
                u8 G                             = row[x * 3 + 1];
                u8 R                             = row[x * 3 + 2];
                (*out_pixels)[destY * width + x] =
                    (0xFFu << 24) | ((u32)R << 16) | ((u32)G << 8) | (u32)B;
            }
        }
    }

    free(buffer);

    return 0;
}
