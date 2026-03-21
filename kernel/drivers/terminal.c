#include <drivers/terminal.h>
#include <lib/font.h>
#include <drivers/uart.h>
#include <lib/string.h>
#include <drivers/framebuffer.h>
#include <lib/ansi.h>
#include <fs/vfs.h>
#include <io/storage.h>
#include <mem/heap.h>
#include <task/process.h>
#include <task/spinlock.h>
#include <stdarg.h>
#include <limits.h>

static struct limine_framebuffer *terminal_fb = nullptr;
static uint8_t *back_buffer                   = nullptr;
static int dirty_y_min                        = INT_MAX;
static int dirty_y_max                        = 0;
static int terminal_x                         = 0;
static int terminal_y                         = 0;
static uint32_t terminal_color                = 0xFFAAAAAA;
static spinlock_t terminal_lock               = {0};
static uint32_t terminal_bg_color             = 0x00000000;
static bool terminal_cursor_visible           = true;
static bool cursor_drawn                      = false;
static int cursor_last_x                      = 0;
static int cursor_last_y                      = 0;
static uint32_t cursor_backing[8 + LINE_SPACING][8];
static bool cursor_overlay_enabled = true; // Enable framebuffer cursor overlay
static bool cursor_batch           = false;
static char boot_log_buffer[32768];
static size_t boot_log_len            = 0;
static size_t boot_log_flushed_len    = 0;
static bool boot_log_ready            = false;
static bool boot_log_flushing         = false;
static constexpr char boot_log_path[] = "/var/log/boot";

static bool boot_log_can_flush(void)
{
    if (!scheduler_is_ready())
        return false;
    if (cpu_in_interrupt())
        return false;
    uint64_t rflags;
    __asm__ volatile("pushfq; pop %0" : "=r"(rflags));
    return (rflags & RFLAGS_IF) != 0;
}

static inline uint8_t *get_draw_surface(void)
{
    return back_buffer ? back_buffer : (uint8_t *)terminal_fb->address;
}

static inline void mark_dirty(int y, int h)
{
    if (!back_buffer)
        return;
    if (y < dirty_y_min)
        dirty_y_min = y;
    int y2 = y + h;
    if (y2 > dirty_y_max)
        dirty_y_max = y2;
}

static void terminal_flush(void)
{
    if (!back_buffer || !terminal_fb || dirty_y_min >= dirty_y_max)
        return;
    int y0 = dirty_y_min < 0 ? 0 : dirty_y_min;
    int y1 = dirty_y_max > (int)terminal_fb->height ? (int)terminal_fb->height : dirty_y_max;
    if (y0 >= y1)
        return;
    size_t pitch  = terminal_fb->pitch;
    size_t offset = (size_t)y0 * pitch;
    size_t bytes  = (size_t)(y1 - y0) * pitch;
    memcpy((uint8_t *)terminal_fb->address + offset, back_buffer + offset, bytes);
    dirty_y_min = INT_MAX;
    dirty_y_max = 0;
}

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

static void cursor_restore(void)
{
    if (!cursor_overlay_enabled)
        return;
    if (!cursor_drawn || !terminal_fb)
        return;

    uint8_t *surface = get_draw_surface();
    for (int row = 0; row < 8 + LINE_SPACING; row++) {
        for (int col = 0; col < 8; col++) {
            uint64_t offset = (cursor_last_y + row) * terminal_fb->pitch + (cursor_last_x + col) * 4;
            if (offset < terminal_fb->pitch * terminal_fb->height)
                memcpy(surface + offset, &cursor_backing[row][col], sizeof(uint32_t));
        }
    }
    mark_dirty(cursor_last_y, 8 + LINE_SPACING);
    cursor_drawn = false;
}

static void cursor_save_and_draw(void)
{
    if (!cursor_overlay_enabled)
        return;
    if (!terminal_fb || !terminal_cursor_visible)
        return;

    uint8_t *surface = get_draw_surface();
    for (int row = 0; row < 8 + LINE_SPACING; row++) {
        for (int col = 0; col < 8; col++) {
            uint64_t offset = (terminal_y + row) * terminal_fb->pitch + (terminal_x + col) * 4;
            if (offset < terminal_fb->pitch * terminal_fb->height)
                memcpy(&cursor_backing[row][col], surface + offset, sizeof(uint32_t));
        }
    }

    for (int row = 6; row < 8; row++) {
        for (int col = 0; col < 8; col++) {
            uint64_t offset = (terminal_y + row) * terminal_fb->pitch + (terminal_x + col) * 4;
            if (offset < terminal_fb->pitch * terminal_fb->height) {
                uint32_t *pixel = (uint32_t *)(surface + offset);
                *pixel          = terminal_color;
            }
        }
    }
    mark_dirty(terminal_y, 8 + LINE_SPACING);
    cursor_last_x = terminal_x;
    cursor_last_y = terminal_y;
    cursor_drawn  = true;
}

void terminal_init(struct limine_framebuffer *fb)
{
    terminal_fb = fb;
    framebuffer_init(fb);
    terminal_x              = terminal_left();
    terminal_y              = terminal_top();
    terminal_bg_color       = 0x00000000;
    terminal_cursor_visible = true;
    cursor_drawn            = false;
    cursor_last_x           = terminal_x;
    cursor_last_y           = terminal_y;
}

void terminal_init_backbuffer(void)
{
    if (!terminal_fb || back_buffer)
        return;
    size_t size = terminal_fb->pitch * terminal_fb->height;
    back_buffer = kmalloc(size);
    if (back_buffer)
        memcpy(back_buffer, (void *)terminal_fb->address, size);
}

void terminal_sync_backbuffer(void)
{
    if (!back_buffer || !terminal_fb)
        return;
    size_t size = terminal_fb->pitch * terminal_fb->height;
    memcpy(back_buffer, (void *)terminal_fb->address, size);
    dirty_y_min = INT_MAX;
    dirty_y_max = 0;
}

void terminal_set_cursor(int x, int y)
{
    cursor_restore();
    terminal_x    = x;
    terminal_y    = y;
    cursor_last_x = x;
    cursor_last_y = y;
    cursor_save_and_draw();
}

void terminal_get_cursor(int *x, int *y)
{
    if (x)
        *x = terminal_x;
    if (y)
        *y = terminal_y;
}

void terminal_get_resolution(int *width, int *height)
{
    if (width)
        *width = terminal_fb ? (int)terminal_fb->width : 0;
    if (height)
        *height = terminal_fb ? (int)terminal_fb->height : 0;
}

void terminal_get_dimensions(int *cols, int *rows)
{
    int w = 0;
    int h = 0;
    terminal_get_resolution(&w, &h);
    if (cols)
        *cols = (w > 0) ? (w / 8) : 0;
    if (rows) {
        int raw = (h > 0) ? (h / (FONT_HEIGHT + LINE_SPACING)) : 0;
        if (raw > 1)
            raw -= 1; // Leave a guard line to avoid accidental scroll/overscan.
        *rows = raw;
    }
}

void terminal_set_color(uint32_t color)
{
    terminal_color = color;
}

void terminal_clear(uint32_t color)
{
    terminal_bg_color = color;
    cursor_restore();
    if (!terminal_fb)
        return;
    uint8_t *surface = get_draw_surface();
    for (size_t y = 0; y < terminal_fb->height; y++) {
        uint32_t *fb_ptr = (uint32_t *)(surface + y * terminal_fb->pitch);
        for (size_t x = 0; x < terminal_fb->width; x++) {
            fb_ptr[x] = color;
        }
    }
    mark_dirty(0, (int)terminal_fb->height);
    terminal_x   = terminal_left();
    terminal_y   = terminal_top();
    cursor_drawn = false;
    terminal_flush();
}

enum AnsiState
{
    ANSI_NORMAL,
    ANSI_ESC,
    ANSI_CSI
};

static enum AnsiState ansi_state = ANSI_NORMAL;
static int ansi_params[16];
static int ansi_param_count   = 0;
static int ansi_current_param = 0;
static bool ansi_bold         = false;
static bool ansi_private      = false;

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
    if (x < 0) {
        w += x;
        x = 0;
    }
    if (y < 0) {
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

    uint8_t *surface = get_draw_surface();

    // Create 64-bit pattern (two pixels)
    uint64_t pattern64 = ((uint64_t)color << 32) | color;

    for (int row = 0; row < h; row++) {
        uint32_t *fb_ptr = (uint32_t *)(surface + (y + row) * terminal_fb->pitch);
        uint32_t *start  = fb_ptr + x;
        int cols         = w;

        // Use 64-bit writes when aligned
        if (((uintptr_t)start & 7) == 0 && cols >= 2) {
            uint64_t *p64 = (uint64_t *)start;
            while (cols >= 2) {
                *p64++ = pattern64;
                cols   -= 2;
            }
            start = (uint32_t *)p64;
        }

        // Handle remaining pixels
        while (cols-- > 0) {
            *start++ = color;
        }
    }
    mark_dirty(y, h);
}

void terminal_scroll(int rows)
{
    if (rows < 1)
        rows = 1;
    if (!terminal_fb)
        return;

    cursor_restore();

    constexpr int char_height = FONT_HEIGHT + LINE_SPACING;
    int scroll_px             = rows * char_height;
    const int fb_height       = (int)terminal_fb->height;
    if (scroll_px > fb_height)
        scroll_px = fb_height;

    const size_t move_bytes = (size_t)(fb_height - scroll_px) * terminal_fb->pitch;
    uint8_t *surface        = get_draw_surface();
    // Use memcpy_forward instead of memmove - we're copying to lower addresses
    // so there's no overlap issue, and the compiler can vectorize this
    memcpy_forward(surface, surface + (size_t)scroll_px * terminal_fb->pitch, move_bytes);
    mark_dirty(0, fb_height);

    terminal_rect_fill(0, fb_height - scroll_px, (int)terminal_fb->width, scroll_px, terminal_bg_color);
    cursor_drawn = false;
}

static void terminal_process_ansi(char cmd)
{
    if (cmd == 'h' || cmd == 'l') {
        bool set_mode = (cmd == 'h');
        if (ansi_private && ansi_param_count > 0) {
            for (int i = 0; i < ansi_param_count; i++) {
                int mode = ansi_params[i];
                if (mode == 25) {
                    if (!set_mode)
                        cursor_restore();
                    terminal_cursor_visible = set_mode;
                    if (set_mode)
                        cursor_save_and_draw();
                    else
                        cursor_drawn = false;
                }
            }
        }
        ansi_private = false;
        return;
    }

    if (cmd == 'm') {
        if (ansi_param_count == 0) {
            terminal_color    = ansi_colors_normal[7];
            terminal_bg_color = ansi_colors_normal[0];
            ansi_bold         = false;
        } else {
            for (int i = 0; i < ansi_param_count; i++) {
                int p = ansi_params[i];
                if (p == 0) {
                    terminal_color    = ansi_colors_normal[7];
                    terminal_bg_color = ansi_colors_normal[0];
                    ansi_bold         = false;
                } else if (p == 1) {
                    ansi_bold = true;
                } else if (p == 22) {
                    ansi_bold = false;
                } else if (p >= 30 && p <= 37) {
                    terminal_color = ansi_bold ? ansi_colors_bright[p - 30] : ansi_colors_normal[p - 30];
                } else if (p >= 40 && p <= 47) {
                    terminal_bg_color = ansi_colors_normal[p - 40]; // Background usually doesn't get bold
                }
            }
        }
    } else if (cmd == 'J') {
        cursor_restore();
        int mode = (ansi_param_count > 0) ? ansi_params[0] : 0;
        if (mode == 2) {
            terminal_rect_fill(terminal_left(),
                               terminal_top(),
                               terminal_right() - terminal_left(),
                               terminal_bottom() - terminal_top(),
                               terminal_bg_color);
            terminal_x = terminal_left();
            terminal_y = terminal_top();
            cursor_save_and_draw();
        } else if (mode == 0) // Clear from cursor to end of screen
        {
            int w = terminal_right() - terminal_x;
            if (w < 0)
                w = 0;
            terminal_rect_fill(terminal_x, terminal_y, w, 8 + LINE_SPACING, terminal_bg_color);
            terminal_rect_fill(terminal_left(),
                               terminal_y + 8 + LINE_SPACING,
                               terminal_right() - terminal_left(),
                               terminal_bottom() - (terminal_y + 8 + LINE_SPACING),
                               terminal_bg_color);
            cursor_save_and_draw();
        } else if (mode == 1) // Clear from beginning of screen to cursor
        {
            terminal_rect_fill(terminal_left(),
                               terminal_top(),
                               terminal_right() - terminal_left(),
                               terminal_y - terminal_top(),
                               terminal_bg_color);
            terminal_rect_fill(terminal_left(),
                               terminal_y,
                               terminal_x - terminal_left() + 8,
                               8 + LINE_SPACING,
                               terminal_bg_color);
            cursor_save_and_draw();
        }
    } else if (cmd == 'K') {
        cursor_restore();
        int mode = (ansi_param_count > 0) ? ansi_params[0] : 0;
        if (mode == 0) // Clear from cursor to end of line
        {
            int w = terminal_right() - terminal_x;
            if (w < 0)
                w = 0;
            terminal_rect_fill(terminal_x, terminal_y, w, 8 + LINE_SPACING, terminal_bg_color);
        } else if (mode == 1) // Clear from beginning of line to cursor
        {
            terminal_rect_fill(terminal_left(),
                               terminal_y,
                               terminal_x - terminal_left(),
                               8 + LINE_SPACING,
                               terminal_bg_color);
        } else if (mode == 2) // Clear entire line
        {
            terminal_rect_fill(terminal_left(),
                               terminal_y,
                               terminal_right() - terminal_left(),
                               8 + LINE_SPACING,
                               terminal_bg_color);
        }
        cursor_save_and_draw();
    } else if (cmd == 'A') // Cursor Up
    {
        int n     = (ansi_param_count > 0 && ansi_params[0] > 0) ? ansi_params[0] : 1;
        int new_y = terminal_y - n * (8 + LINE_SPACING);
        if (new_y < terminal_top())
            new_y = terminal_top();
        terminal_set_cursor(terminal_x, new_y);
    } else if (cmd == 'B') // Cursor Down
    {
        int n     = (ansi_param_count > 0 && ansi_params[0] > 0) ? ansi_params[0] : 1;
        int new_y = terminal_y + n * (8 + LINE_SPACING);
        if (terminal_fb && new_y >= terminal_bottom())
            new_y = terminal_bottom() - (8 + LINE_SPACING);
        terminal_set_cursor(terminal_x, new_y);
    } else if (cmd == 'C') // Cursor Forward
    {
        int n     = (ansi_param_count > 0 && ansi_params[0] > 0) ? ansi_params[0] : 1;
        int new_x = terminal_x + n * 8;
        if (terminal_fb && new_x >= terminal_right())
            new_x = terminal_right() - 8;
        terminal_set_cursor(new_x, terminal_y);
    } else if (cmd == 'D') // Cursor Backward
    {
        int n     = (ansi_param_count > 0 && ansi_params[0] > 0) ? ansi_params[0] : 1;
        int new_x = terminal_x - n * 8;
        if (new_x < terminal_left())
            new_x = terminal_left();
        terminal_set_cursor(new_x, terminal_y);
    } else if (cmd == 'H' || cmd == 'f') {
        int row   = (ansi_param_count > 0 && ansi_params[0] > 0) ? ansi_params[0] - 1 : 0;
        int col   = (ansi_param_count > 1 && ansi_params[1] > 0) ? ansi_params[1] - 1 : 0;
        int new_x = terminal_left() + col * 8;
        int new_y = terminal_top() + row * (8 + LINE_SPACING);
        if (terminal_fb) {
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
    if (c == '\r') {
        terminal_x = terminal_left();
        return;
    }

    if (c == '\n') {
        terminal_x       = terminal_left();
        terminal_y       += 8 + LINE_SPACING;
        int bottom_limit = terminal_bottom();
        if (terminal_y + 8 + LINE_SPACING > bottom_limit) {
            terminal_scroll(1);
            terminal_y -= (8 + LINE_SPACING);
        }
        return;
    }

    if (c == '\b' || c == 0x7F) {
        terminal_x -= 8;
        if (terminal_x < terminal_left()) {
            terminal_x = terminal_left();
            return;
        }
        uint8_t *surface = get_draw_surface();
        for (int row = 0; row < 8 + LINE_SPACING; row++) {
            for (int col = 0; col < 8; col++) {
                uint64_t offset = (terminal_y + row) * terminal_fb->pitch + (terminal_x + col) * 4;
                if (offset < terminal_fb->pitch * terminal_fb->height) {
                    uint32_t *pixel = (uint32_t *)(surface + offset);
                    *pixel          = terminal_bg_color;
                }
            }
        }
        mark_dirty(terminal_y, 8 + LINE_SPACING);
        return;
    }

    if (c < 32 || c > 126)
        c = '?';

    const uint8_t *glyph = font8x8_basic[c - 32];
    uint8_t *surface     = get_draw_surface();

    for (int row = 0; row < 8 + LINE_SPACING; row++) {
        for (int col = 0; col < 8; col++) {
            uint64_t offset = (terminal_y + row) * terminal_fb->pitch + (terminal_x + col) * 4;
            if (offset < terminal_fb->pitch * terminal_fb->height) {
                uint32_t *pixel = (uint32_t *)(surface + offset);

                bool is_fg = false;
                if (row < 8) {
                    if ((glyph[row] >> (7 - col)) & 1)
                        is_fg = true;
                }

                *pixel = is_fg ? terminal_color : terminal_bg_color;
            }
        }
    }
    mark_dirty(terminal_y, 8 + LINE_SPACING);
    terminal_x      += 8;
    int right_limit = terminal_right();
    if (terminal_x + 8 > right_limit) {
        terminal_x       = terminal_left();
        terminal_y       += 8 + LINE_SPACING;
        int bottom_limit = terminal_bottom();
        if (terminal_y + 8 + LINE_SPACING > bottom_limit) {
            terminal_scroll(1);
            terminal_y -= (8 + LINE_SPACING);
        }
    }
}

void terminal_putc(char c)
{
    uart_putc(c);
    if (!terminal_fb)
        return;

    if (!cursor_batch) {
        cursor_restore();
        cursor_drawn = false;
    }

    if (ansi_state == ANSI_NORMAL) {
        if (c == '\033') {
            ansi_state = ANSI_ESC;
        } else {
            terminal_draw_char(c);
        }
    } else if (ansi_state == ANSI_ESC) {
        if (c == '[') {
            ansi_state         = ANSI_CSI;
            ansi_param_count   = 0;
            ansi_current_param = 0;
            ansi_private       = false;
            for (int i = 0; i < 16; i++)
                ansi_params[i] = 0;
        } else if (c == 'c') {
            terminal_color    = ansi_colors_normal[7];
            terminal_bg_color = ansi_colors_normal[0];
            ansi_bold         = false;
            terminal_clear(terminal_bg_color);
            terminal_x = 0;
            terminal_y = 0;
            ansi_state = ANSI_NORMAL;
        } else {
            ansi_state = ANSI_NORMAL;
            terminal_draw_char(c);
        }
    } else if (ansi_state == ANSI_CSI) {
        if (c >= '0' && c <= '9') {
            ansi_current_param = ansi_current_param * 10 + (c - '0');
        } else if (c == '?') {
            ansi_private = true;
        } else if (c == ';') {
            if (ansi_param_count < 16)
                ansi_params[ansi_param_count++] = ansi_current_param;
            ansi_current_param = 0;
        } else {
            if (ansi_param_count < 16)
                ansi_params[ansi_param_count++] = ansi_current_param;
            terminal_process_ansi(c);
            ansi_state   = ANSI_NORMAL;
            ansi_private = false;
        }
    }

    if (!cursor_batch) {
        if (terminal_cursor_visible && !cursor_drawn)
            cursor_save_and_draw();
        else if (!terminal_cursor_visible)
            cursor_drawn = false;
        terminal_flush();
    }
}

void terminal_write(const char *data, size_t size)
{
    spinlock_acquire(&terminal_lock);

    bool prev_batch = cursor_batch;
    cursor_batch    = true;

    cursor_restore(); // Erase cursor at old position before writing
    cursor_drawn = false;

    for (size_t i = 0; i < size; i++)
        terminal_putc(data[i]);

    cursor_batch = prev_batch;

    if (cursor_overlay_enabled) {
        if (terminal_cursor_visible && !cursor_drawn)
            cursor_save_and_draw();
        else if (!terminal_cursor_visible)
            cursor_drawn = false;
    }
    terminal_flush();

    spinlock_release(&terminal_lock);
}

void terminal_write_string(const char *data)
{
    spinlock_acquire(&terminal_lock);

    bool prev_batch = cursor_batch;
    cursor_batch    = true;

    cursor_restore(); // Erase cursor at old position before writing
    cursor_drawn = false;

    while (*data)
        terminal_putc(*data++);

    cursor_batch = prev_batch;

    if (cursor_overlay_enabled) {
        if (terminal_cursor_visible && !cursor_drawn)
            cursor_save_and_draw();
        else if (!terminal_cursor_visible)
            cursor_drawn = false;
    }
    terminal_flush();

    spinlock_release(&terminal_lock);
}

static void terminal_putc_callback(char c, void *arg)
{
    (void)arg;
    if (c == '\n') {
        terminal_putc('\r');
        terminal_putc('\n');
    } else if (c == '\t') {
        int n = 4 - (terminal_x & 3);
        for (int i = 0; i < n; i++)
            terminal_putc(' ');
    } else {
        terminal_putc(c);
    }
}

#ifdef TEST_MODE
#define TEST_CAPTURE_SIZE 8192
static char test_capture_buf[TEST_CAPTURE_SIZE];
static size_t test_capture_pos  = 0;
static bool test_capture_active = false;

void test_capture_begin(void)
{
    test_capture_active = true;
    test_capture_pos    = 0;
}

void test_capture_discard(void)
{
    test_capture_active = false;
    test_capture_pos    = 0;
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
    if (test_capture_active) {
        // Render into the capture buffer without touching the terminal.
        if (test_capture_pos < TEST_CAPTURE_SIZE - 1) {
            // Limit to remaining space to avoid overflow.
            size_t remaining = TEST_CAPTURE_SIZE - 1 - test_capture_pos;
            size_t written   = vsnprintk(test_capture_buf + test_capture_pos, remaining, format, args);
            test_capture_pos += (written < remaining) ? written : remaining;
        }
        return;
    }
#endif

    spinlock_acquire(&terminal_lock);

    // Batch all output to avoid per-character flush
    bool prev_batch = cursor_batch;
    cursor_batch    = true;
    cursor_restore();
    cursor_drawn = false;

    va_list args_copy;
    va_copy(args_copy, args); // NOLINT(clang-analyzer-security.VAList, clang-analyzer-valist.Uninitialized)
    vcbprintf(nullptr, terminal_putc_callback, format, &args_copy);
    va_end(args_copy);

    cursor_batch = prev_batch;
    if (cursor_overlay_enabled && terminal_cursor_visible && !cursor_drawn)
        cursor_save_and_draw();
    terminal_flush();

    spinlock_release(&terminal_lock);
}

void printk(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    vprintk(format, args);
    va_end(args);
}

static bool boot_log_busy = false; // Prevent re-entrant boot log writes

static vfs_inode_t *boot_log_open(void)
{
    if (!vfs_root)
        return nullptr;

    vfs_inode_t *node = vfs_resolve_path(boot_log_path);
    if (!node) {
        if (vfs_mknod((char *)boot_log_path, VFS_FILE, 0) != 0)
            return nullptr;
        node = vfs_resolve_path(boot_log_path);
    }
    return node;
}

static void boot_log_record(const char *line)
{
    // Keep boot logs buffered during tests to avoid disk I/O interference.
    if (!line)
        return;

    if (boot_log_busy)
        return;
    boot_log_busy = true;

    size_t len = strlen(line);
    if (len > sizeof(boot_log_buffer) - 1)
        len = sizeof(boot_log_buffer) - 1;

    if (boot_log_len + len < sizeof(boot_log_buffer)) {
        memcpy(boot_log_buffer + boot_log_len, line, len);
        boot_log_len                  += len;
        boot_log_buffer[boot_log_len] = '\0';
    }

    boot_log_busy = false;

#ifndef TEST_MODE
    if (!boot_log_flushing && boot_log_can_flush())
        boot_log_flush();
#endif
}

void boot_log_flush(void)
{
#ifdef TEST_MODE
    return;
#endif
    if (boot_log_flushing)
        return;
    if (!boot_log_can_flush())
        return;
    if (!vfs_root)
        return;

    boot_log_flushing = true;

    vfs_inode_t *node = boot_log_open();
    if (!node) {
        boot_log_flushing = false;
        return;
    }

    if (!boot_log_ready) {
        vfs_truncate(node);
        boot_log_flushed_len = 0;
        boot_log_ready       = true;
    }

    const size_t flushed_before = boot_log_flushed_len;
    while (boot_log_flushed_len < boot_log_len) {
        size_t pending   = boot_log_len - boot_log_flushed_len;
        uint64_t written = vfs_write(node,
                                     boot_log_flushed_len,
                                     pending,
                                     (uint8_t *)(boot_log_buffer + boot_log_flushed_len));
        if (written != pending)
            break;
        boot_log_flushed_len += pending;
    }

    vfs_release(node);

    if (boot_log_flushed_len != flushed_before) {
        storage_flush(0);
    }

    boot_log_flushing = false;
}

void boot_message(boot_log_level_t level, const char *fmt, ...)
{
    const char *level_str;
    switch (level) {
    case INFO:
        level_str = "INFO";
        printk(KWHT "[ " KBGRN "INFO" KRESET " ] ");
        break;
    case WARNING:
        level_str = "WARNING";
        printk(KWHT "[ " KYEL "WARNING" KRESET " ] ");
        break;
    case ERROR:
        level_str = "ERROR";
        printk(KWHT "[ " KRED "ERROR" KRESET " ] ");
        break;
    default:
        level_str = "INFO";
        break;
    }

    va_list ap = {};
    va_start(ap, fmt);
    char buf[512];
    vsnprintk(buf, sizeof(buf), fmt, ap);
    va_end(ap); // NOLINT(clang-analyzer-security.VAList, clang-analyzer-valist.Uninitialized)
    printk("%s\n", buf);

    // Also append to boot log (plain text)
    char line[640];
    snprintk(line, sizeof(line), "[%s] %s\n", level_str, buf);
    boot_log_record(line);
}
