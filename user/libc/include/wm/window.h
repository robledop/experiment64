#pragma once

#include <wm/video_context.h>
#include <stdint.h>
#include <list.h>

#define WIN_BGCOLOR 0xFFDDDDDD
#define WIN_TITLE_COLOR 0xFF009070
#define WIN_TITLE_COLOR_INACTIVE 0xFF004040
#define WIN_TITLE_TEXT_COLOR 0xFFFFE0E0
#define WIN_TITLE_TEXT_COLOR_INACTIVE 0xFFBBBBBB
#define WIN_BORDER_COLOR 0xFF555599
#define WIN_TITLE_HEIGHT 25
#define WIN_BORDER_WIDTH 2
#define WIN_TEXT_COLOR 0xFF111111
#define WIN_TITLE_MARGIN ((WIN_TITLE_HEIGHT - VESA_CHAR_HEIGHT) / 2)

#define WIN_NODECORATION 0x1
#define WIN_CLOSEABLE 0x2
#define WIN_RESIZABLE 0x4

struct window;

typedef void (*WindowPaintHandler)(const struct window *);
typedef void (*WindowMousedownHandler)(const struct window *, int16_t, int16_t);
typedef void (*WindowResizeHandler)(const struct window *, uint16_t, uint16_t);
typedef void (*WindowCloseHandler)(const struct window *);

typedef struct window {
    struct window *parent;
    int16_t x;
    int16_t y;
    uint16_t width;
    uint16_t height;
    uint16_t flags;
    video_context_t *context;
    struct window *drag_child;
    struct window *resize_child;
    struct window *active_child;
    list_t *children;
    uint16_t drag_off_x;
    uint16_t drag_off_y;
    uint16_t resize_start_mouse_x;
    uint16_t resize_start_mouse_y;
    uint16_t resize_start_width;
    uint16_t resize_start_height;
    uint64_t drag_last_move_ms;
    uint8_t last_button_state;
    WindowPaintHandler paint_function;
    WindowMousedownHandler mousedown_function;
    WindowResizeHandler resize_function;
    WindowCloseHandler close_function;
    char *title;
} window_t;

window_t *window_new(int16_t x, int16_t y, uint16_t width, uint16_t height, uint16_t flags, video_context_t *context);
int window_init(window_t *window, int16_t x, int16_t y, uint16_t width, uint16_t height, uint16_t flags,
                video_context_t *context);
int window_screen_x(const window_t *window);
int window_screen_y(const window_t *window);
void window_paint(window_t *window, list_t *dirty_regions, uint8_t paint_children);
void window_process_mouse(window_t *window, uint16_t mouse_x, uint16_t mouse_y, uint8_t mouse_buttons);
void window_paint_handler(const window_t *window);
void window_mousedown_handler(const window_t *window, int16_t x, int16_t y);
list_t *window_get_windows_above(const window_t *parent, window_t *child);
list_t *window_get_windows_below(const window_t *parent, window_t *child);
void window_raise(window_t *window, uint8_t do_draw);
void window_move(window_t *window, int new_x, int new_y);
void window_resize(window_t *window, int new_width, int new_height);
window_t *window_create_window(window_t *window, int16_t x, int16_t y, uint16_t width, int16_t height, uint16_t flags);
void window_insert_child(window_t *window, window_t *child);
void window_invalidate(window_t *window, int top, int left, int bottom, int right);
void window_set_title(window_t *window, const char *new_title);
// void window_append_title(window_t *window, const char *additional_chars);
