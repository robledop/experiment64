#pragma once

#include <stddef.h>
#include <stdint.h>
#include <wm/video_context.h>

#define WIN_BGCOLOR 0xFFDEDDDA
#define WIN_TITLE_COLOR 0xFF1C71D8
#define WIN_TITLE_COLOR_INACTIVE 0xFF1A5FB4
#define WIN_TITLE_TEXT_COLOR 0xFFF6F5F4
#define WIN_TITLE_TEXT_COLOR_INACTIVE 0xFFDEDDDA
#define WIN_BORDER_COLOR_ACTIVE 0xFF99C1F1
#define WIN_BORDER_COLOR_INACTIVE 0xFF1C71D8
#define WIN_CLOSE_BUTTON_BG_COLOR 0XFFE01B24
#define WIN_CLOSE_BUTTON_FG_COLOR 0XFFFFFFFF
#define WIN_MINIMIZE_BUTTON_BG_COLOR 0XFF2EC27E
#define WIN_MINIMIZE_BUTTON_FG_COLOR 0XFFFFFFFF
#define WIN_TITLE_HEIGHT 25
#define WIN_BORDER_WIDTH 2
#define WIN_TEXT_COLOR 0xFF241F31
#define WIN_TITLE_MARGIN ((WIN_TITLE_HEIGHT - VESA_CHAR_HEIGHT) / 2)

#define WIN_NODECORATION 0x1
#define WIN_CLOSEABLE 0x2
#define WIN_RESIZABLE 0x4
#define WIN_BACKGROUND 0x8
#define WIN_BUTTON 0x10
#define WIN_MINIMIZED 0x20
#define WIN_MINIMIZABLE 0x40

struct window;

typedef void (*WindowPaintHandler)(const struct window *);
typedef void (*WindowMousedownHandler)(struct window *, int16_t, int16_t);
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
    struct window **children;
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

/**
 * @brief Allocate and initialize a widget whose first member is a window_t.
 *
 * Performs the shared button/textbox/etc. factory pattern: allocate @p size
 * bytes, run window_init() on the embedded window_t, install @p paint as the
 * paint callback, and free on failure. The widget struct must start with a
 * `window_t window;` field so the cast is valid.
 *
 * @param size  Total size of the widget struct (e.g. sizeof(button_t)).
 * @param paint Custom paint handler; may be nullptr to keep the default.
 * @return Pointer to the new widget, or nullptr on allocation failure.
 */
void *widget_new(size_t size, int16_t x, int16_t y, uint16_t width, uint16_t height,
                 uint16_t flags, WindowPaintHandler paint);
int window_screen_x(const window_t *window);
int window_screen_y(const window_t *window);
void window_paint(window_t *window, rect_t **dirty_regions, uint8_t paint_children);
void window_process_mouse(window_t *window, uint16_t mouse_x, uint16_t mouse_y, uint8_t mouse_buttons);
void window_paint_handler(const window_t *window);
void window_mousedown_handler(window_t *window, int16_t x, int16_t y);
window_t **window_get_windows_above(const window_t *parent, window_t *child);
window_t **window_get_windows_below(const window_t *parent, window_t *child);
void window_raise(window_t *window, uint8_t do_draw);
void window_minimize(window_t *window);
void window_restore(window_t *window);
void window_move(window_t *window, int new_x, int new_y);
void window_resize(window_t *window, int new_width, int new_height);
window_t *window_create_window(window_t *window, int16_t x, int16_t y, uint16_t width, int16_t height, uint16_t flags);
void window_insert_child(window_t *window, window_t *child);
void window_remove_child(window_t *parent, window_t *child);
void window_invalidate(window_t *window, int top, int left, int bottom, int right);
void window_set_title(window_t *window, const char *new_title);

/**
 * @brief Callback type invoked when a window is raised.
 *
 * The WM server registers this to update its taskbar. Client applications
 * can leave it unset. Called with the window being raised.
 */
typedef void (*WindowRaiseNotifyHandler)(window_t *window);

/**
 * @brief Register a callback to be notified when any window is raised.
 * @param handler Callback function, or nullptr to clear.
 */
void window_set_raise_notify(WindowRaiseNotifyHandler handler);
