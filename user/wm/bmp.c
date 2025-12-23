#include <wm/bmp.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdint.h>

enum { BI_RGB = 0, BI_BITFIELDS = 3 };

__attribute__((target("avx,sse2")))
int bitmap_load_argb(const char *path, uint32_t **out_pixels, uint32_t *out_width, uint32_t *out_height)
{
    if (!out_pixels) {
        return -1;
    }
    *out_pixels = nullptr;
    if (out_width) {
        *out_width = 0;
    }
    if (out_height) {
        *out_height = 0;
    }

    const int fd = open(path, O_RDONLY);
    if (fd == -1) {
        return -1;
    }

    struct stat file_stat;
    if (fstat(fd, &file_stat) != 0) {
        close(fd);
        return -1;
    }

    if (file_stat.size < (sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER))) {
        close(fd);
        return -1;
    }

    uint8_t *buffer = malloc((size_t)file_stat.size);
    if (!buffer) {
        close(fd);
        return -1;
    }

    const int nread = read(fd, buffer, (size_t)file_stat.size);
    close(fd);
    if (nread != (int)file_stat.size) {
        free(buffer);
        return -1;
    }

    BITMAPFILEHEADER *fh = (BITMAPFILEHEADER *)buffer;
    BITMAPINFOHEADER ih = *(BITMAPINFOHEADER *)(buffer + sizeof(BITMAPFILEHEADER));

    if (fh->bfType != 0x4D42) {
        free(buffer);
        return -1;
    }

    if (ih.biPlanes != 1) {
        free(buffer);
        return -1;
    }

    if (ih.biBitCount != 24) {
        free(buffer);
        return -1;
    }

    if (!(ih.biCompression == BI_RGB || (ih.biCompression == BI_BITFIELDS && ih.biBitCount == 32))) {
        free(buffer);
        return -1;
    }

    const int width = ih.biWidth;
    const int height = (ih.biHeight > 0) ? ih.biHeight : -ih.biHeight;
    const int top_down = (ih.biHeight < 0);

    if (width <= 0 || height <= 0) {
        free(buffer);
        return -1;
    }

    // Bounds check: ensure pixel data fits within the file.
    const uint64_t bytes_per_row = (uint64_t)(((width * 3 + 3) / 4) * 4);
    const uint64_t pixel_bytes = bytes_per_row * (uint64_t)height;
    if ((uint64_t)fh->bfOffBits >= (uint64_t)file_stat.size ||
        (uint64_t)fh->bfOffBits + pixel_bytes > (uint64_t)file_stat.size) {
        free(buffer);
        return -1;
    }

    const uint64_t pixel_count = (uint64_t)width * (uint64_t)height;
    // Avoid relying on SIZE_MAX (not guaranteed in this libc); use a portable upper bound instead.
    if (pixel_count > (uint64_t)((size_t)-1) / sizeof(uint32_t)) {
        free(buffer);
        return -1;
    }

    *out_pixels = (uint32_t *)malloc((size_t)pixel_count * sizeof(uint32_t));
    if (!*out_pixels) {
        free(buffer);
        return -1;
    }

    if (ih.biBitCount == 24) {
        for (int y = 0; y < height; y++) {
            uint8_t *row = buffer + fh->bfOffBits + (uint64_t)y * bytes_per_row;

            int destY = top_down ? y : (height - 1 - y);
            for (int x = 0; x < width; x++) {
                uint8_t B = row[x * 3 + 0];
                uint8_t G = row[x * 3 + 1];
                uint8_t R = row[x * 3 + 2];
                (*out_pixels)[destY * width + x] =
                    (0xFFu << 24) | ((uint32_t)R << 16) | ((uint32_t)G << 8) | (uint32_t)B;
            }
        }
    }

    if (out_width) {
        *out_width = (uint32_t)width;
    }
    if (out_height) {
        *out_height = (uint32_t)height;
    }

    free(buffer);

    return 0;
}

