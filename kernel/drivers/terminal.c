#include "terminal.h"
#include "font.h"
#include "uart.h"
#include "string.h"
#include "framebuffer.h"
#include <stdarg.h>
#include <limits.h>

#define KRESET "\033[0m"
#define KRED "\033[31m"
#define KYEL "\033[33m"
#define KBGRN "\033[1;32m"
#define KWHT "\033[37m"

static struct limine_framebuffer *terminal_fb = NULL;
static int terminal_x = 0;
static int terminal_y = 0;
static uint32_t terminal_color = 0xFFAAAAAA;
static uint32_t terminal_bg_color = 0x00000000;

#define LINE_SPACING 5

static inline int terminal_left(void)
{
    return TERMINAL_MARGIN;
}

static inline int terminal_top(void)
{
    return TERMINAL_MARGIN;
}

static inline int terminal_right(void)
{
    return terminal_fb ? (int)terminal_fb->width - TERMINAL_MARGIN : 0;
}

static inline int terminal_bottom(void)
{
    return terminal_fb ? (int)terminal_fb->height - TERMINAL_MARGIN : 0;
}

static void terminal_draw_cursor(int x, int y, uint32_t color)
{
    if (!terminal_fb)
        return;
    for (int row = 6; row < 8; row++)
    {
        for (int col = 0; col < 8; col++)
        {
            uint64_t offset = (y + row) * terminal_fb->pitch + (x + col) * 4;
            if (offset < terminal_fb->pitch * terminal_fb->height)
            {
                uint32_t *pixel = (uint32_t *)((uint8_t *)terminal_fb->address + offset);
                *pixel = color;
            }
        }
    }
}

void terminal_init(struct limine_framebuffer *fb)
{
    terminal_fb = fb;
    framebuffer_init(fb);
    terminal_x = terminal_left();
    terminal_y = terminal_top();
    terminal_bg_color = 0x00000000;
}

void terminal_set_cursor(int x, int y)
{
    if (terminal_fb)
        terminal_draw_cursor(terminal_x, terminal_y, terminal_bg_color);
    terminal_x = x;
    terminal_y = y;
    if (terminal_fb)
        terminal_draw_cursor(terminal_x, terminal_y, terminal_color);
}

void terminal_get_cursor(int *x, int *y)
{
    if (x)
        *x = terminal_x;
    if (y)
        *y = terminal_y;
}

void terminal_set_color(uint32_t color)
{
    terminal_color = color;
}

void terminal_clear(uint32_t color)
{
    terminal_bg_color = color;
    if (!terminal_fb)
        return;
    for (size_t y = 0; y < terminal_fb->height; y++)
    {
        uint32_t *fb_ptr = (uint32_t *)((uint8_t *)terminal_fb->address + y * terminal_fb->pitch);
        for (size_t x = 0; x < terminal_fb->width; x++)
        {
            fb_ptr[x] = color;
        }
    }
    terminal_x = terminal_left();
    terminal_y = terminal_top();
    terminal_draw_cursor(terminal_x, terminal_y, terminal_color);
}

enum AnsiState
{
    ANSI_NORMAL,
    ANSI_ESC,
    ANSI_CSI
};

static enum AnsiState ansi_state = ANSI_NORMAL;
static int ansi_params[16];
static int ansi_param_count = 0;
static int ansi_current_param = 0;
static bool ansi_bold = false;

static uint32_t ansi_colors_normal[] = {
    0xFF000000, // 0: Black
    0xFFAA0000, // 1: Red
    0xFF00AA00, // 2: Green
    0xFFAA5500, // 3: Brown
    0xFF0000AA, // 4: Blue
    0xFFAA00AA, // 5: Magenta
    0xFF00AAAA, // 6: Cyan
    0xFFAAAAAA  // 7: Light Gray
};

static uint32_t ansi_colors_bright[] = {
    0xFF555555, // 0: Dark Gray
    0xFFFF5555, // 1: Bright Red
    0xFF55FF55, // 2: Bright Green
    0xFFFFFF55, // 3: Yellow
    0xFF5555FF, // 4: Bright Blue
    0xFFFF55FF, // 5: Bright Magenta
    0xFF55FFFF, // 6: Bright Cyan
    0xFFFFFFFF  // 7: White
};

static void terminal_rect_fill(int x, int y, int w, int h, uint32_t color)
{
    if (!terminal_fb)
        return;
    if (x < 0)
    {
        w += x;
        x = 0;
    }
    if (y < 0)
    {
        h += y;
        y = 0;
    }
    if (x >= (int)terminal_fb->width || y >= (int)terminal_fb->height)
        return;
    const int max_w = (terminal_fb->width > (uint64_t)INT_MAX) ? INT_MAX : (int)terminal_fb->width;
    const int max_h = (terminal_fb->height > (uint64_t)INT_MAX) ? INT_MAX : (int)terminal_fb->height;
    if (x + w > max_w)
        w = max_w - x;
    if (y + h > max_h)
        h = max_h - y;

    for (int row = 0; row < h; row++)
    {
        uint32_t *fb_ptr = (uint32_t *)((uint8_t *)terminal_fb->address + (y + row) * terminal_fb->pitch);
        for (int col = 0; col < w; col++)
        {
            fb_ptr[x + col] = color;
        }
    }
}

static void terminal_scroll(void)
{
    if (!terminal_fb)
        return;

    int char_height = 8 + LINE_SPACING;
    int left = terminal_left();
    int right = terminal_right();
    int top = terminal_top();
    int bottom = terminal_bottom();
    int usable_height = bottom - top;
    if (usable_height <= char_height)
    {
        terminal_rect_fill(left, top, right - left, usable_height, terminal_bg_color);
        terminal_x = left;
        terminal_y = top;
        return;
    }

    int row_bytes = (right - left) * 4;
    for (int row = 0; row < usable_height - char_height; row++)
    {
        uint8_t *dst = (uint8_t *)terminal_fb->address + (top + row) * terminal_fb->pitch + left * 4;
        uint8_t *src = dst + char_height * terminal_fb->pitch;
        memcpy(dst, src, row_bytes);
    }

    terminal_rect_fill(left, bottom - char_height, right - left, char_height, terminal_bg_color);
}

static void terminal_process_ansi(char cmd)
{
    if (cmd == 'm')
    {
        if (ansi_param_count == 0)
        {
            terminal_color = ansi_colors_normal[7];
            terminal_bg_color = ansi_colors_normal[0];
            ansi_bold = false;
        }
        else
        {
            for (int i = 0; i < ansi_param_count; i++)
            {
                int p = ansi_params[i];
                if (p == 0)
                {
                    terminal_color = ansi_colors_normal[7];
                    terminal_bg_color = ansi_colors_normal[0];
                    ansi_bold = false;
                }
                else if (p == 1)
                {
                    ansi_bold = true;
                }
                else if (p == 22)
                {
                    ansi_bold = false;
                }
                else if (p >= 30 && p <= 37)
                {
                    terminal_color = ansi_bold ? ansi_colors_bright[p - 30] : ansi_colors_normal[p - 30];
                }
                else if (p >= 40 && p <= 47)
                {
                    terminal_bg_color = ansi_colors_normal[p - 40]; // Background usually doesn't get bold
                }
            }
        }
    }
    else if (cmd == 'J')
    {
        int mode = (ansi_param_count > 0) ? ansi_params[0] : 0;
        if (mode == 2)
        {
            terminal_rect_fill(terminal_left(), terminal_top(), terminal_right() - terminal_left(), terminal_bottom() - terminal_top(), terminal_bg_color);
            terminal_x = terminal_left();
            terminal_y = terminal_top();
            terminal_draw_cursor(terminal_x, terminal_y, terminal_color);
        }
        else if (mode == 0) // Clear from cursor to end of screen
        {
            int w = terminal_right() - terminal_x;
            if (w < 0)
                w = 0;
            terminal_rect_fill(terminal_x, terminal_y, w, 8 + LINE_SPACING, terminal_bg_color);
            terminal_rect_fill(terminal_left(), terminal_y + 8 + LINE_SPACING, terminal_right() - terminal_left(), terminal_bottom() - (terminal_y + 8 + LINE_SPACING), terminal_bg_color);
            terminal_draw_cursor(terminal_x, terminal_y, terminal_color);
        }
        else if (mode == 1) // Clear from beginning of screen to cursor
        {
            terminal_rect_fill(terminal_left(), terminal_top(), terminal_right() - terminal_left(), terminal_y - terminal_top(), terminal_bg_color);
            terminal_rect_fill(terminal_left(), terminal_y, terminal_x - terminal_left() + 8, 8 + LINE_SPACING, terminal_bg_color);
            terminal_draw_cursor(terminal_x, terminal_y, terminal_color);
        }
    }
    else if (cmd == 'K')
    {
        int mode = (ansi_param_count > 0) ? ansi_params[0] : 0;
        if (mode == 0) // Clear from cursor to end of line
        {
            int w = terminal_right() - terminal_x;
            if (w < 0)
                w = 0;
            terminal_rect_fill(terminal_x, terminal_y, w, 8 + LINE_SPACING, terminal_bg_color);
        }
        else if (mode == 1) // Clear from beginning of line to cursor
        {
            terminal_rect_fill(terminal_left(), terminal_y, terminal_x - terminal_left(), 8 + LINE_SPACING, terminal_bg_color);
        }
        else if (mode == 2) // Clear entire line
        {
            terminal_rect_fill(terminal_left(), terminal_y, terminal_right() - terminal_left(), 8 + LINE_SPACING, terminal_bg_color);
        }
        terminal_draw_cursor(terminal_x, terminal_y, terminal_color);
    }
    else if (cmd == 'A') // Cursor Up
    {
        int n = (ansi_param_count > 0 && ansi_params[0] > 0) ? ansi_params[0] : 1;
        int new_y = terminal_y - n * (8 + LINE_SPACING);
        if (new_y < terminal_top())
            new_y = terminal_top();
        terminal_set_cursor(terminal_x, new_y);
    }
    else if (cmd == 'B') // Cursor Down
    {
        int n = (ansi_param_count > 0 && ansi_params[0] > 0) ? ansi_params[0] : 1;
        int new_y = terminal_y + n * (8 + LINE_SPACING);
        if (terminal_fb && new_y >= terminal_bottom())
            new_y = terminal_bottom() - (8 + LINE_SPACING);
        terminal_set_cursor(terminal_x, new_y);
    }
    else if (cmd == 'C') // Cursor Forward
    {
        int n = (ansi_param_count > 0 && ansi_params[0] > 0) ? ansi_params[0] : 1;
        int new_x = terminal_x + n * 8;
        if (terminal_fb && new_x >= terminal_right())
            new_x = terminal_right() - 8;
        terminal_set_cursor(new_x, terminal_y);
    }
    else if (cmd == 'D') // Cursor Backward
    {
        int n = (ansi_param_count > 0 && ansi_params[0] > 0) ? ansi_params[0] : 1;
        int new_x = terminal_x - n * 8;
        if (new_x < terminal_left())
            new_x = terminal_left();
        terminal_set_cursor(new_x, terminal_y);
    }
    else if (cmd == 'H' || cmd == 'f')
    {
        int row = (ansi_param_count > 0 && ansi_params[0] > 0) ? ansi_params[0] - 1 : 0;
        int col = (ansi_param_count > 1 && ansi_params[1] > 0) ? ansi_params[1] - 1 : 0;
        int new_x = terminal_left() + col * 8;
        int new_y = terminal_top() + row * (8 + LINE_SPACING);
        if (terminal_fb)
        {
            if (new_x >= terminal_right())
                new_x = terminal_right() - 8;
            if (new_y >= terminal_bottom())
                new_y = terminal_bottom() - (8 + LINE_SPACING);
        }
        terminal_set_cursor(new_x, new_y);
    }
}

static void terminal_draw_char(char c)
{
    if (c == '\n')
    {
        // Clear the cursor at the old position (by drawing background)
        terminal_draw_cursor(terminal_x, terminal_y, terminal_bg_color);

        terminal_x = terminal_left();
        terminal_y += 8 + LINE_SPACING;
        int bottom_limit = terminal_bottom();
        if (terminal_y + 8 + LINE_SPACING > bottom_limit)
        {
            terminal_scroll();
            terminal_y -= (8 + LINE_SPACING);
        }
        terminal_draw_cursor(terminal_x, terminal_y, terminal_color);
        return;
    }

    if (c == '\b')
    {
        terminal_draw_cursor(terminal_x, terminal_y, terminal_bg_color);
        terminal_x -= 8;
        if (terminal_x < terminal_left())
            terminal_x = terminal_left();
        terminal_draw_cursor(terminal_x, terminal_y, terminal_color);
        return;
    }

    if (c < 32 || c > 126)
        c = '?';

    const uint8_t *glyph = font8x8_basic[c - 32];

    for (int row = 0; row < 8 + LINE_SPACING; row++)
    {
        for (int col = 0; col < 8; col++)
        {
            uint64_t offset = (terminal_y + row) * terminal_fb->pitch + (terminal_x + col) * 4;
            if (offset < terminal_fb->pitch * terminal_fb->height)
            {
                uint32_t *pixel = (uint32_t *)((uint8_t *)terminal_fb->address + offset);

                bool is_fg = false;
                if (row < 8)
                {
                    if ((glyph[row] >> (7 - col)) & 1)
                        is_fg = true;
                }

                *pixel = is_fg ? terminal_color : terminal_bg_color;
            }
        }
    }
    terminal_x += 8;
    int right_limit = terminal_right();
    if (terminal_x + 8 > right_limit)
    {
        terminal_x = terminal_left();
        terminal_y += 8 + LINE_SPACING;
        int bottom_limit = terminal_bottom();
        if (terminal_y + 8 + LINE_SPACING > bottom_limit)
        {
            terminal_scroll();
            terminal_y -= (8 + LINE_SPACING);
        }
    }
    terminal_draw_cursor(terminal_x, terminal_y, terminal_color);
}

void terminal_putc(char c)
{
    uart_putc(c);
    if (!terminal_fb)
        return;

    if (ansi_state == ANSI_NORMAL)
    {
        if (c == '\033')
        {
            ansi_state = ANSI_ESC;
        }
        else
        {
            terminal_draw_char(c);
        }
    }
    else if (ansi_state == ANSI_ESC)
    {
        if (c == '[')
        {
            ansi_state = ANSI_CSI;
            ansi_param_count = 0;
            ansi_current_param = 0;
            for (int i = 0; i < 16; i++)
                ansi_params[i] = 0;
        }
        else if (c == 'c')
        {
            terminal_color = ansi_colors_normal[7];
            terminal_bg_color = ansi_colors_normal[0];
            ansi_bold = false;
            terminal_clear(terminal_bg_color);
            terminal_x = 0;
            terminal_y = 0;
            ansi_state = ANSI_NORMAL;
        }
        else
        {
            ansi_state = ANSI_NORMAL;
            terminal_draw_char(c);
        }
    }
    else if (ansi_state == ANSI_CSI)
    {
        if (c >= '0' && c <= '9')
        {
            ansi_current_param = ansi_current_param * 10 + (c - '0');
        }
        else if (c == ';')
        {
            if (ansi_param_count < 16)
                ansi_params[ansi_param_count++] = ansi_current_param;
            ansi_current_param = 0;
        }
        else
        {
            if (ansi_param_count < 16)
                ansi_params[ansi_param_count++] = ansi_current_param;
            terminal_process_ansi(c);
            ansi_state = ANSI_NORMAL;
        }
    }
}

void terminal_write(const char *data, size_t size)
{
    for (size_t i = 0; i < size; i++)
        terminal_putc(data[i]);
}

void terminal_write_string(const char *data)
{
    while (*data)
        terminal_putc(*data++);
}

static void terminal_putc_callback(char c, void *arg)
{
    (void)arg;
    terminal_putc(c);
}

#ifdef TEST_MODE
#define TEST_CAPTURE_SIZE 8192
static char test_capture_buf[TEST_CAPTURE_SIZE];
static size_t test_capture_pos = 0;
static bool test_capture_active = false;

void test_capture_begin(void)
{
    test_capture_active = true;
    test_capture_pos = 0;
}

void test_capture_discard(void)
{
    test_capture_active = false;
    test_capture_pos = 0;
}

void test_capture_flush(void)
{
    test_capture_active = false;
    if (test_capture_pos == 0)
        return;
    // Null-terminate and print the buffered output.
    if (test_capture_pos >= TEST_CAPTURE_SIZE)
        test_capture_pos = TEST_CAPTURE_SIZE - 1;
    test_capture_buf[test_capture_pos] = '\0';
    printk("%s", test_capture_buf);
    test_capture_pos = 0;
}
#endif

void vprintk(const char *format, va_list args)
{
#ifdef TEST_MODE
    if (test_capture_active)
    {
        // Render into the capture buffer without touching the terminal.
        if (test_capture_pos < TEST_CAPTURE_SIZE - 1)
        {
            // Limit to remaining space to avoid overflow.
            size_t remaining = TEST_CAPTURE_SIZE - 1 - test_capture_pos;
            size_t written = vsnprintk(test_capture_buf + test_capture_pos, remaining, format, args);
            test_capture_pos += (written < remaining) ? written : remaining;
        }
        return;
    }
#endif
    vcbprintf(NULL, terminal_putc_callback, format, args);
}

void printk(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    vprintk(format, args);
    va_end(args);
}

void boot_message(t level, const char *fmt, ...)
{
    switch (level)
    {
    case INFO:
        printk(KWHT "[ " KBGRN "INFO" KRESET " ] ");
        break;
    case WARNING:
        printk(KWHT "[ " KYEL "WARNING" KRESET " ] ");
        break;
    case ERROR:
        printk(KWHT "[ " KRED "ERROR" KRESET " ] ");
        break;
    }

    va_list ap;
    va_start(ap, fmt);
    char buf[512];
    vsnprintk(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    printk("%s\n", buf);
}
