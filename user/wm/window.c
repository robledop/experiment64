#include <stdint.h>
#include <wm/window.h>
#include <wm/video_context.h>
#include <wm/rect.h>
#include <array.h>
#include <sys/time.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static constexpr int WIN_RESIZE_HANDLE_SIZE          = 14;
static constexpr uint64_t WIN_DRAG_FRAME_INTERVAL_MS = 16;

static const uint8_t glyph_close[7] = {0x00, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x00};

static void draw_close_button_icon(const video_context_t *context, const int x, const int y, const int scale,
                                   const uint32_t color)
{
    for (int row = 0; row < 7; row++) {
        uint8_t bits = glyph_close[row];
        for (int col = 0; col < 5; col++) {
            if (!(bits & (uint8_t)(1u << (4 - col))))
                continue;
            context_fill_rect(context, x + col * scale, y + row * scale, scale, scale, color);
        }
    }
}

static bool window_close_clicked(const window_t *window, uint16_t x, uint16_t y)
{
    int close_x = window->x + window->width - WIN_TITLE_HEIGHT + WIN_BORDER_WIDTH;
    int close_y = window->y + WIN_BORDER_WIDTH;
    return x >= close_x && x < close_x + WIN_TITLE_HEIGHT && y >= close_y && y < close_y + WIN_TITLE_HEIGHT;
}

static uint64_t window_now_ms(void)
{
    struct timeval tv = {0};
    if (gettimeofday(&tv, nullptr) != 0)
        return 0;
    return (uint64_t)tv.tv_sec * 1000u + (uint64_t)tv.tv_usec / 1000u;
}

static bool window_children_push(window_t *parent, window_t *child)
{
    return arr_try_push(parent->children, child);
}

window_t *window_new(int16_t x, int16_t y, uint16_t width, uint16_t height, uint16_t flags, video_context_t *context)
{
    window_t *window = malloc(sizeof(window_t));
    if (!window) {
        return window;
    }

    if (!window_init(window, x, y, width, height, flags, context)) {
        free(window);
        return nullptr;
    }

    return window;
}

int window_init(window_t *window, int16_t x, int16_t y, uint16_t width, uint16_t height, uint16_t flags,
                video_context_t *context)
{
    window->children             = nullptr;
    window->x                    = x;
    window->y                    = y;
    window->width                = width;
    window->height               = height;
    window->context              = context;
    window->flags                = flags;
    window->parent               = nullptr;
    window->drag_child           = nullptr;
    window->resize_child         = nullptr;
    window->drag_off_x           = 0;
    window->drag_off_y           = 0;
    window->resize_start_mouse_x = 0;
    window->resize_start_mouse_y = 0;
    window->resize_start_width   = 0;
    window->resize_start_height  = 0;
    window->drag_last_move_ms    = 0;
    window->last_button_state    = 0;
    window->paint_function       = window_paint_handler;
    window->mousedown_function   = window_mousedown_handler;
    window->close_function       = nullptr;
    window->resize_function      = nullptr;
    window->active_child         = nullptr;
    window->title                = nullptr;

    return 1;
}

int window_screen_x(const window_t *window)
{
    if (window->parent) {
        return window->x + window_screen_x(window->parent);
    }

    return window->x;
}

int window_screen_y(const window_t *window)
{
    if (window->parent) {
        return window->y + window_screen_y(window->parent);
    }

    return window->y;
}

static void window_draw_border(window_t *window)
{
    const int screen_x = window_screen_x(window);
    const int screen_y = window_screen_y(window);

    if (!window) {
        panic("window is null");
        return;
    }
    bool is_active = window->parent->active_child == window;

    for (int i = 0; i < WIN_BORDER_WIDTH; i++) {
        context_draw_rect(window->context,
                          screen_x + i,
                          screen_y + i,
                          window->width - (2 * i),
                          window->height - (2 * i),
                          is_active ? WIN_BORDER_COLOR_ACTIVE : WIN_BORDER_COLOR_INACTIVE);
    }

    for (int i = 1; i <= WIN_BORDER_WIDTH; i++) {
        context_horizontal_line(window->context,
                                screen_x + WIN_BORDER_WIDTH,
                                screen_y + (WIN_TITLE_HEIGHT - i),
                                window->width - (2 * WIN_BORDER_WIDTH),
                                is_active ? WIN_BORDER_COLOR_ACTIVE : WIN_BORDER_COLOR_INACTIVE);
    }

    // Draw title bar
    context_fill_rect(window->context,
                      screen_x + WIN_BORDER_WIDTH,
                      screen_y + WIN_BORDER_WIDTH,
                      window->width - (2 * WIN_BORDER_WIDTH),
                      (WIN_TITLE_HEIGHT - (2 * WIN_BORDER_WIDTH)),
                      is_active ? WIN_TITLE_COLOR : WIN_TITLE_COLOR_INACTIVE);

    if (window->flags & WIN_CLOSEABLE) {
        // Draw close button
        int close_x = screen_x + window->width - WIN_BORDER_WIDTH - WIN_TITLE_HEIGHT + (2 * WIN_BORDER_WIDTH);
        int close_y = screen_y + WIN_BORDER_WIDTH;
        context_fill_rect(window->context,
                          close_x,
                          close_y,
                          WIN_TITLE_HEIGHT - (2 * WIN_BORDER_WIDTH),
                          WIN_TITLE_HEIGHT - (2 * WIN_BORDER_WIDTH),
                          WIN_CLOSE_BUTTON_BG_COLOR);

        draw_close_button_icon(window->context, close_x + 6, close_y + 3, 2, WIN_CLOSE_BUTTON_FG_COLOR);
    }

    if (window->title) {
        context_draw_text(window->context,
                          window->title,
                          screen_x + WIN_TITLE_MARGIN,
                          screen_y + WIN_TITLE_MARGIN,
                          window->parent->active_child == window
                          ? WIN_TITLE_TEXT_COLOR
                          : WIN_TITLE_TEXT_COLOR_INACTIVE);
    }
}

static void window_apply_bound_clipping(window_t *window, int in_recursion, rect_t **dirty_regions)
{
    if (!window || !window->context)
        return;

    int screen_x = window_screen_x(window);
    int screen_y = window_screen_y(window);

    rect_t *temp_rect;
    if ((!(window->flags & WIN_NODECORATION)) && in_recursion) {
        screen_x  += WIN_BORDER_WIDTH;
        screen_y  += WIN_TITLE_HEIGHT;
        temp_rect = rect_new(screen_y,
                             screen_x,
                             screen_y + window->height - WIN_TITLE_HEIGHT - WIN_BORDER_WIDTH - 1,
                             screen_x + window->width - (2 * WIN_BORDER_WIDTH) - 1);
    } else {
        temp_rect = rect_new(screen_y, screen_x, screen_y + window->height - 1, screen_x + window->width - 1);
    }
    if (!temp_rect) {
        context_clear_clip_rects(window->context);
        window->context->clipping_on = 1;
        return;
    }

    const int max_x = (int)window->context->width - 1;
    const int max_y = (int)window->context->height - 1;
    if (temp_rect->left < 0)
        temp_rect->left = 0;
    if (temp_rect->top < 0)
        temp_rect->top = 0;
    if (temp_rect->right > max_x)
        temp_rect->right = max_x;
    if (temp_rect->bottom > max_y)
        temp_rect->bottom = max_y;
    if (temp_rect->left > temp_rect->right || temp_rect->top > temp_rect->bottom) {
        context_clear_clip_rects(window->context);
        window->context->clipping_on = 1;
        free(temp_rect);
        return;
    }

    if (!window->parent) {
        if (dirty_regions) {
            size_t dirty_count = arr_len(dirty_regions);
            if (dirty_count > 0) {
                size_t snapshot_bytes = sizeof(*dirty_regions) * dirty_count;
                auto dirty_snapshot = (rect_t **)malloc(snapshot_bytes);
                if (dirty_snapshot) {
                    memcpy(dirty_snapshot, dirty_regions, snapshot_bytes);

                    for (size_t i = 0; i < dirty_count; i++) {
                        rect_t *current_dirty_rect = dirty_snapshot[i];
                        if (!current_dirty_rect) {
                            continue;
                        }
                        rect_t *clone_dirty_rect = rect_new(current_dirty_rect->top,
                                                            current_dirty_rect->left,
                                                            current_dirty_rect->bottom,
                                                            current_dirty_rect->right);

                        if (clone_dirty_rect) {
                            context_add_clip_rect(window->context, clone_dirty_rect);
                        }
                    }

                    free(dirty_snapshot);
                }
            }

            context_intersect_clip_rect(window->context, temp_rect);
        } else {
            context_add_clip_rect(window->context, temp_rect);
        }

        return;
    }

    window_apply_bound_clipping(window->parent, 1, dirty_regions);

    context_intersect_clip_rect(window->context, temp_rect);

    window_t **clip_windows = window_get_windows_above(window->parent, window);
    size_t clip_count       = arr_len(clip_windows);
    for (size_t i = 0; i < clip_count; i++) {
        window_t *clipping_window = clip_windows[i];
        if (!clipping_window) {
            continue;
        }

        screen_x = window_screen_x(clipping_window);
        screen_y = window_screen_y(clipping_window);

        temp_rect =
            rect_new(screen_y, screen_x, screen_y + clipping_window->height - 1, screen_x + clipping_window->width - 1);
        context_subtract_clip_rect(window->context, temp_rect);
        free(temp_rect);
    }

    arr_free(clip_windows);
}

void window_update_title(window_t *window)
{
    if (!window->context) {
        return;
    }

    if (window->flags & WIN_NODECORATION) {
        return;
    }

    window_apply_bound_clipping(window, 0, nullptr);
    window_draw_border(window);
    context_clear_clip_rects(window->context);
}

void window_invalidate(window_t *window, int top, int left, int bottom, int right)
{
    int origin_x = window_screen_x(window);
    int origin_y = window_screen_y(window);
    top          += origin_y;
    bottom       += origin_y;
    left         += origin_x;
    right        += origin_x;

    rect_t **dirty_regions = nullptr;

    rect_t *dirty_rect = rect_new(top, left, bottom, right);
    if (!dirty_rect) {
        return;
    }

    if (!arr_try_push(dirty_regions, dirty_rect)) {
        free(dirty_rect);
        arr_free(dirty_regions);
        return;
    }

    window_paint(window, dirty_regions, 0);

    arr_free(dirty_regions);
    free(dirty_rect);
}

void window_paint(window_t *window, rect_t **dirty_regions, uint8_t paint_children)
{
    window_t *current_child;
    rect_t *temp_rect;

    if (!window->context) {
        return;
    }

    window_apply_bound_clipping(window, 0, dirty_regions);

    int screen_x = window_screen_x(window);
    int screen_y = window_screen_y(window);

    if (!(window->flags & WIN_NODECORATION)) {
        window_draw_border(window);

        screen_x  += WIN_BORDER_WIDTH;
        screen_y  += WIN_TITLE_HEIGHT;
        temp_rect = rect_new(screen_y,
                             screen_x,
                             screen_y + window->height - WIN_TITLE_HEIGHT - WIN_BORDER_WIDTH - 1,
                             screen_x + window->width - (2 * WIN_BORDER_WIDTH) - 1);
        context_intersect_clip_rect(window->context, temp_rect);
    }

    size_t child_count = arr_len(window->children);
    for (size_t i = 0; i < child_count; i++) {
        current_child = arr_get(window->children, i);
        if (!current_child) {
            continue;
        }

        int child_screen_x = window_screen_x(current_child);
        int child_screen_y = window_screen_y(current_child);

        temp_rect = rect_new(child_screen_y,
                             child_screen_x,
                             child_screen_y + current_child->height - 1,
                             child_screen_x + current_child->width - 1);
        context_subtract_clip_rect(window->context, temp_rect);
        free(temp_rect);
    }

    window->context->translate_x = screen_x;
    window->context->translate_y = screen_y;
    window->paint_function(window);

    context_clear_clip_rects(window->context);
    window->context->translate_x = 0;
    window->context->translate_y = 0;

    if (!paint_children) {
        return;
    }

    child_count = arr_len(window->children);
    for (size_t i = 0; i < child_count; i++) {
        current_child = arr_get(window->children, i);
        if (!current_child) {
            continue;
        }

        if (dirty_regions) {
            size_t dirty_count = arr_len(dirty_regions);
            size_t j;
            for (j = 0; j < dirty_count; j++) {
                temp_rect = dirty_regions[j];
                if (!temp_rect) {
                    continue;
                }

                screen_x = window_screen_x(current_child);
                screen_y = window_screen_y(current_child);

                if (temp_rect->left <= (screen_x + current_child->width - 1) && temp_rect->right >= screen_x &&
                    temp_rect->top <= (screen_y + current_child->height - 1) && temp_rect->bottom >= screen_y) {
                    break;
                }
            }

            if (j == dirty_count) {
                continue;
            }
        }

        window_paint(current_child, dirty_regions, 1);
    }
}

void window_paint_handler(const window_t *window)
{
    context_fill_rect(window->context, 0, 0, window->width, window->height, WIN_BGCOLOR);
}

window_t **window_get_windows_above(const window_t *parent, window_t *child)
{
    window_t **return_list = nullptr;

    if (!parent || !child || !parent->children)
        return nullptr;

    ptrdiff_t idx = arr_find(parent->children, child);
    if (idx < 0) {
        return return_list;
    }

    size_t child_count = arr_len(parent->children);
    for (size_t i = (size_t)idx + 1; i < child_count; i++) {
        window_t *current_window = arr_get(parent->children, i);
        if (!current_window) {
            continue;
        }

        if (current_window->x <= (child->x + child->width - 1) &&
            (current_window->x + current_window->width - 1) >= child->x &&
            current_window->y <= (child->y + child->height - 1) &&
            (current_window->y + current_window->height - 1) >= child->y) {
            arr_push(return_list, current_window);
        }
    }

    return return_list;
}

window_t **window_get_windows_below(const window_t *parent, window_t *child)
{
    window_t **return_list = nullptr;

    if (!parent || !child || !parent->children)
        return nullptr;

    ptrdiff_t idx = arr_find(parent->children, child);
    if (idx < 0) {
        return return_list;
    }

    for (ptrdiff_t i = idx - 1; i >= 0; i--) {
        window_t *current_window = arr_get(parent->children, i);
        if (!current_window) {
            continue;
        }

        if (current_window->x <= (child->x + child->width - 1) &&
            (current_window->x + current_window->width - 1) >= child->x &&
            current_window->y <= (child->y + child->height - 1) &&
            (current_window->y + current_window->height - 1) >= child->y) {
            arr_push(return_list, current_window);
        }
    }

    return return_list;
}

void window_raise(window_t *window, uint8_t do_draw)
{
    if (!window->parent) {
        return;
    }

    window_t *parent = window->parent;

    if (parent->active_child == window) {
        return;
    }

    window_t *last_active = parent->active_child;

    ptrdiff_t idx = arr_find(parent->children, window);
    if (idx < 0) {
        return;
    }

    size_t child_count      = arr_len(parent->children);
    window_t *raised_window = parent->children[idx];
    for (size_t j = (size_t)idx + 1; j < child_count; j++) {
        parent->children[j - 1] = parent->children[j];
    }
    parent->children[child_count - 1] = raised_window;

    parent->active_child = window;

    if (!do_draw) {
        return;
    }

    window_paint(window, nullptr, 1);

    if (last_active) {
        window_update_title(last_active);
    }
}

void window_move(window_t *window, int new_x, int new_y)
{
    if (!window)
        return;

    int old_x = window->x;
    int old_y = window->y;

    if (old_x == new_x && old_y == new_y)
        return;

    rect_t new_window_rect;

    window_raise(window, 0);

    window_apply_bound_clipping(window, 0, nullptr);

    window->x = (int16_t)new_x;
    window->y = (int16_t)new_y;

    new_window_rect.top    = window_screen_y(window);
    new_window_rect.left   = window_screen_x(window);
    new_window_rect.bottom = new_window_rect.top + window->height - 1;
    new_window_rect.right  = new_window_rect.left + window->width - 1;

    window->x = (int16_t)old_x;
    window->y = (int16_t)old_y;

    context_subtract_clip_rect(window->context, &new_window_rect);

    rect_t **replacement_list   = nullptr;
    rect_t **dirty_list         = window->context->clip_rects;
    window->context->clip_rects = replacement_list;
    rect_t **dirty_regions      = nullptr;

    window->x = (int16_t)new_x;
    window->y = (int16_t)new_y;

    size_t dirty_list_count = arr_len(dirty_list);
    for (size_t i = 0; i < dirty_list_count; i++) {
        rect_t *dirty_rect = arr_get(dirty_list, i);
        if (!dirty_rect)
            continue;
        arr_push(dirty_regions, dirty_rect);
    }

    window_paint(window->parent, dirty_regions, 1);

    for (size_t i = 0; i < dirty_list_count; i++) {
        free(arr_get(dirty_list, i));
    }

    arr_free(dirty_regions);
    arr_free(dirty_list);

    window_paint(window, nullptr, 1);
}

static window_t *window_root(window_t *window)
{
    if (!window)
        return nullptr;

    while (window->parent)
        window = window->parent;

    return window;
}

void window_resize(window_t *window, int new_width, int new_height)
{
    if (!window)
        return;

    int min_width  = 1;
    int min_height = 1;
    if (!(window->flags & WIN_NODECORATION)) {
        min_width  = (2 * WIN_BORDER_WIDTH) + VESA_CHAR_WIDTH;
        min_height = WIN_TITLE_HEIGHT + WIN_BORDER_WIDTH + VESA_LINE_HEIGHT;
    }

    if (new_width < min_width)
        new_width = min_width;
    if (new_height < min_height)
        new_height = min_height;

    if ((uint16_t)new_width == window->width && (uint16_t)new_height == window->height)
        return;

    const uint16_t old_width  = window->width;
    const uint16_t old_height = window->height;

    window->width  = (uint16_t)new_width;
    window->height = (uint16_t)new_height;

    if (window->resize_function)
        window->resize_function(window, old_width, old_height);

    window_t *root = window_root(window);
    if (root)
        window_paint(root, nullptr, 1);
}

void window_process_mouse(window_t *window, uint16_t mouse_x, uint16_t mouse_y, uint8_t mouse_buttons)
{
    if (!window) {
        return;
    }

    uint8_t left_click     = mouse_buttons;
    uint8_t was_left_click = window->last_button_state;

    size_t child_count = arr_len(window->children);
    for (size_t i = child_count; i-- > 0;) {
        window_t *child = window->children[i];
        if (!child) {
            continue;
        }

        if (!(mouse_x >= child->x && mouse_x < (child->x + child->width) && mouse_y >= child->y &&
            mouse_y < (child->y + child->height))) {
            continue;
        }

        if (left_click && !was_left_click) {

            if (!(child->flags & WIN_NODECORATION) &&
                (child->flags & WIN_CLOSEABLE) &&
                window_close_clicked(child, mouse_x, mouse_y)) {
                child->close_function(child);
                return;
            }

            window_raise(child, 1);

            if (!(child->flags & WIN_NODECORATION) && mouse_y >= child->y && mouse_y < (child->y + 31)) {
                window->drag_off_x        = mouse_x - child->x;
                window->drag_off_y        = mouse_y - child->y;
                window->drag_child        = child;
                window->drag_last_move_ms = 0;

                break;
            }

            if (!(child->flags & WIN_NODECORATION) &&
                mouse_x >= child->x + child->width - WIN_RESIZE_HANDLE_SIZE &&
                mouse_y >= child->y + child->height - WIN_RESIZE_HANDLE_SIZE) {
                window->resize_child         = child;
                window->resize_start_mouse_x = mouse_x;
                window->resize_start_mouse_y = mouse_y;
                window->resize_start_width   = child->width;
                window->resize_start_height  = child->height;
                break;
            }
        }

        window_process_mouse(child, mouse_x - child->x, mouse_y - child->y, mouse_buttons);
        break;
    }

    if (window->drag_child) {
        const int target_x = (int)mouse_x - (int)window->drag_off_x;
        const int target_y = (int)mouse_y - (int)window->drag_off_y;

        if (left_click) {
            const uint64_t now_ms = window_now_ms();
            if (window->drag_last_move_ms == 0 ||
                now_ms - window->drag_last_move_ms >= WIN_DRAG_FRAME_INTERVAL_MS) {
                window_move(window->drag_child, target_x, target_y);
                window->drag_last_move_ms = now_ms;
            }
        } else if (was_left_click) {
            window_move(window->drag_child, target_x, target_y);
        }
    } else if (window->resize_child && (window->resize_child->flags & WIN_RESIZABLE)) {
        int new_width  = (int)window->resize_start_width + (int)mouse_x - (int)window->resize_start_mouse_x;
        int new_height = (int)window->resize_start_height + (int)mouse_y - (int)window->resize_start_mouse_y;
        window_resize(window->resize_child, new_width, new_height);
    }

    if (!left_click) {
        window->drag_child        = nullptr;
        window->resize_child      = nullptr;
        window->drag_last_move_ms = 0;
    }

    if (window->mousedown_function && left_click && !was_left_click) {
        window->mousedown_function(window, (int16_t)mouse_x, (int16_t)mouse_y);
    }

    window->last_button_state = mouse_buttons;
}


void window_mousedown_handler(const window_t *window, int16_t x,
                              int16_t y)
{
}

static void window_update_context(window_t *window, video_context_t *context)
{
    window->context = context;

    size_t child_count = arr_len(window->children);
    for (size_t i = 0; i < child_count; i++) {
        window_t *child = window->children[i];
        if (!child) {
            continue;
        }
        window_update_context(child, context);
    }
}

void window_remove_child(window_t *parent, window_t *child)
{
    if (parent) {
        ptrdiff_t idx = arr_find(parent->children, child);
        if (idx >= 0)
            arr_remove_at(parent->children, (size_t)idx);
        if (parent->active_child == child)
            parent->active_child = nullptr;
    }
}

void window_insert_child(window_t *window, window_t *child)
{
    if (!window || !child)
        return;

    if (child->parent) {
        ptrdiff_t old_idx = arr_find(child->parent->children, child);
        if (old_idx >= 0)
            arr_remove_at(child->parent->children, (size_t)old_idx);
        if (child->parent->active_child == child)
            child->parent->active_child = nullptr;
    }

    child->parent = window;
    if (arr_find(window->children, child) < 0)
        arr_push(window->children, child);
    window->active_child = child;

    window_update_context(child, window->context);
}

window_t *window_create_window(window_t *window, int16_t x, int16_t y, uint16_t width, int16_t height, uint16_t flags)
{
    window_t *new_window = window_new(x, y, width, height, flags, window->context);
    if (!new_window) {
        return new_window;
    }

    if (!window_children_push(window, new_window)) {
        free(new_window);
        return nullptr;
    }

    new_window->parent               = window;
    new_window->parent->active_child = new_window;

    return new_window;
}

void window_set_title(window_t *window, const char *new_title)
{
    if (window->title) {
        free(window->title);
    }

    int len = (int)strlen(new_title);

    window->title = (char *)malloc((len + 1) * sizeof(char));
    if (!window->title) {
        return;
    }

    memcpy(window->title, new_title, len + 1);

    if (window->flags & WIN_NODECORATION) {
        window_invalidate(window, 0, 0, window->height - 1, window->width - 1);
    } else {
        window_update_title(window);
    }
}