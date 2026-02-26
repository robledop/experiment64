#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/ioctl.h>

static void plot(volatile uint32_t *fb, int x, int y, uint32_t color, int width, int height, int pitch_bytes)
{
    if (!fb || x < 0 || x >= width || y < 0 || y >= height)
        return;
    const int stride = pitch_bytes / (int)sizeof(uint32_t);
    fb[y * stride + x] = color;
}

static void usage(void)
{
    printf("Usage: fbline x1 y1 x2 y2 color\n");
}

static int hex_value(char c)
{
    if (c >= '0' && c <= '9')
    {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f')
    {
        return 10 + (c - 'a');
    }
    if (c >= 'A' && c <= 'F')
    {
        return 10 + (c - 'A');
    }
    return -1;
}

static uint32_t parse_color(const char* arg)
{
    if (arg[0] == '0' && (arg[1] == 'x' || arg[1] == 'X'))
    {
        arg += 2;
        uint32_t value = 0;
        while (*arg)
        {
            int digit = hex_value(*arg++);
            if (digit < 0)
            {
                break;
            }
            value = (value << 4) | (uint32_t)digit;
        }
        return value;
    }
    return (uint32_t)strtol(arg, nullptr, 10);
}

int main(int argc, char** argv)
{
    if (argc != 6)
    {
        usage();
        exit(0);
    }

    int x1 = (int)strtol(argv[1], nullptr, 10);
    int y1 = (int)strtol(argv[2], nullptr, 10);
    const int x2 = (int)strtol(argv[3], nullptr, 10);
    const int y2 = (int)strtol(argv[4], nullptr, 10);
    const uint32_t color = parse_color(argv[5]);

    const int fd = open("/dev/fb0", O_RDWR);
    if (fd < 0)
    {
        printf("fbline: unable to open /dev/fb0\n");
        exit(0);
    }

    uint32_t fb_width = 0, fb_height = 0, fb_pitch = 0;
    if (ioctl(fd, FB_IOCTL_GET_WIDTH, &fb_width) != 0 ||
        ioctl(fd, FB_IOCTL_GET_HEIGHT, &fb_height) != 0 ||
        ioctl(fd, FB_IOCTL_GET_PITCH, &fb_pitch) != 0)
    {
        printf("fbline: ioctl failed to query framebuffer geometry\n");
        close(fd);
        exit(0);
    }

    const size_t fb_size = (size_t)fb_pitch * fb_height;
    void* map = mmap(nullptr, fb_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (map == MAP_FAILED)
    {
        printf("fbline: mmap failed\n");
        close(fd);
        exit(0);
    }
    close(fd);

    volatile uint32_t* fb = map;

    const int dx = abs(x2 - x1);
    const int sx = x1 < x2 ? 1 : -1;
    const int dy = -abs(y2 - y1);
    const int sy = y1 < y2 ? 1 : -1;
    int err = dx + dy;

    for (;;)
    {
        plot(fb, x1, y1, color, (int)fb_width, (int)fb_height, (int)fb_pitch);
        if (x1 == x2 && y1 == y2)
        {
            break;
        }
        const int e2 = 2 * err;
        if (e2 >= dy)
        {
            err += dy;
            x1 += sx;
        }
        if (e2 <= dx)
        {
            err += dx;
            y1 += sy;
        }
    }

    munmap((void*)fb, fb_size);
    exit(0);

    return 0;
}
