#include <wm/imui.h>
#include <wm/window.h>
#include <array.h>
#include <stdlib.h>
#include <string.h>

/**
 * @brief Clamp a value to a lower bound.
 *
 * @param value Input value.
 * @param min_value Minimum allowed value.
 * @return The clamped value.
 */
static int imui_clamp_min(const int value, const int min_value)
{
    return value < min_value ? min_value : value;
}

/**
 * @brief Clamp a value to an upper bound.
 *
 * @param value Input value.
 * @param max_value Maximum allowed value.
 * @return The clamped value.
 */
static int imui_clamp_max(const int value, const int max_value)
{
    return value > max_value ? max_value : value;
}

/**
 * @brief Expand the frame dirty rectangle to include a region.
 *
 * @param ui UI context.
 * @param x Region x coordinate.
 * @param y Region y coordinate.
 * @param width Region width.
 * @param height Region height.
 */
static void imui_track_dirty(imui_context_t *ui, const int x, const int y, const uint16_t width, const uint16_t height)
{
    if (!ui || !ui->window || width == 0 || height == 0)
        return;

    int left   = x;
    int top    = y;
    int right  = x + (int)width - 1;
    int bottom = y + (int)height - 1;

    const int max_right  = (int)ui->window->width - 1;
    const int max_bottom = (int)ui->window->height - 1;

    if (right < 0 || bottom < 0 || left > max_right || top > max_bottom)
        return;

    left   = imui_clamp_min(left, 0);
    top    = imui_clamp_min(top, 0);
    right  = imui_clamp_max(right, max_right);
    bottom = imui_clamp_max(bottom, max_bottom);

    if (!ui->has_dirty) {
        ui->has_dirty    = true;
        ui->dirty_left   = (int16_t)left;
        ui->dirty_top    = (int16_t)top;
        ui->dirty_right  = (int16_t)right;
        ui->dirty_bottom = (int16_t)bottom;
        return;
    }

    if (left < ui->dirty_left)
        ui->dirty_left = (int16_t)left;
    if (top < ui->dirty_top)
        ui->dirty_top = (int16_t)top;
    if (right > ui->dirty_right)
        ui->dirty_right = (int16_t)right;
    if (bottom > ui->dirty_bottom)
        ui->dirty_bottom = (int16_t)bottom;
}

/**
 * @brief Point the drawing context at the current back buffer.
 *
 * @param ui UI context.
 */
static void imui_sync_context(imui_context_t *ui)
{
    if (!ui || !ui->context || !ui->window)
        return;

    ui->context->buffer      = ui->window->buffer;
    ui->context->width       = ui->window->width;
    ui->context->height      = ui->window->height;
    ui->context->pitch       = (uint32_t)ui->window->width * 4u;
    ui->context->translate_x = 0;
    ui->context->translate_y = 0;
    context_clear_clip_rects(ui->context);
}

/**
 * @brief Update per-frame mouse state from a WM mouse event.
 *
 * @param ui UI context.
 * @param mouse_event Optional mouse event for this frame.
 */
static void imui_update_mouse(imui_context_t *ui, const wm_event_mouse_t *mouse_event)
{
    if (!ui)
        return;

    ui->mouse_x       = -1;
    ui->mouse_y       = -1;
    ui->mouse_pressed = false;
    ui->mouse_down    = false;
    ui->mouse_inside  = false;

    if (!ui || !mouse_event || !ui->window)
        return;

    if (mouse_event->window_id != ui->window->window_id)
        return;

    int x = mouse_event->x;
    int y = mouse_event->y;

    if (!(ui->window->flags & WIN_NODECORATION)) {
        x -= WIN_BORDER_WIDTH;
        y -= WIN_TITLE_HEIGHT;
    }

    ui->mouse_x = (int16_t)x;
    ui->mouse_y = (int16_t)y;

    ui->mouse_pressed = (mouse_event->buttons != 0);
    ui->mouse_down    = (mouse_event->buttons != 0);

    if (x < 0 || y < 0 || x >= (int)ui->window->width || y >= (int)ui->window->height)
        return;

    ui->mouse_inside = true;
}

imui_style_t imui_style_default()
{
    return (imui_style_t){
        .background_color = 0xFF1C1F24u,
        .button_color = 0xFFE7EAEEu,
        .button_hot_color = 0xFFF3F5F8u,
        .button_active_color = 0xFFD7DCE2u,
        .button_border_color = 0xFF2A2F36u,
        .text_color = 0xFF111111u,
        .item_spacing_x = 5,
        .item_spacing_y = 5,
    };
}

/**
 * @brief Initialize an immediate-mode UI context for a WM window.
 *
 * @param ui UI context to initialize.
 * @param window Target WM window.
 * @return true on success, false otherwise.
 */
bool imui_init(imui_context_t *ui, wm_window_t *window)
{
    if (!ui || !window)
        return false;

    memset(ui, 0, sizeof(*ui));
    ui->window = window;

    ui->context = context_new(window->buffer, window->width, window->height, (uint32_t)window->width * 4u);
    if (!ui->context)
        return false;

    ui->style = imui_style_default();

    return true;
}

/**
 * @brief Release resources owned by an immediate-mode UI context.
 *
 * @param ui UI context.
 */
void imui_deinit(imui_context_t *ui)
{
    if (!ui || !ui->context)
        return;

    context_clear_clip_rects(ui->context);
    arr_free(ui->context->clip_rects);

    free(ui->context);
    ui->context = nullptr;
}

/**
 * @brief Start a new UI frame.
 *
 * @param ui UI context.
 * @param mouse_event Optional mouse event used for this frame.
 */
void imui_begin_frame(imui_context_t *ui, const wm_event_mouse_t *mouse_event)
{
    if (!ui || !ui->window || !ui->context)
        return;

    imui_sync_context(ui);

    ui->cursor_x     = 0;
    ui->cursor_y     = 0;
    ui->line_start_x = 0;
    ui->line_start_y = 0;
    ui->line_height  = 0;
    ui->last_item_x  = 0;
    ui->last_item_y  = 0;
    ui->last_item_w  = 0;
    ui->last_item_h  = 0;
    ui->id_counter   = 0;
    ui->hot_id       = 0;
    ui->has_dirty    = false;

    imui_update_mouse(ui, mouse_event);

    if (!ui->mouse_down)
        ui->active_id = 0;
}

/**
 * @brief End the current UI frame and present dirty pixels.
 *
 * @param ui UI context.
 */
void imui_end_frame(imui_context_t *ui)
{
    if (!ui || !ui->window || !ui->has_dirty)
        return;

    const int width  = ui->dirty_right - ui->dirty_left + 1;
    const int height = ui->dirty_bottom - ui->dirty_top + 1;

    if (width > 0 && height > 0) {
        wm_invalidate_region(ui->window,
                             ui->dirty_left,
                             ui->dirty_top,
                             (uint16_t)width,
                             (uint16_t)height);
    }

    ui->has_dirty = false;
}

/**
 * @brief Set layout cursor position for the next widget.
 *
 * @param ui UI context.
 * @param x New cursor x coordinate.
 * @param y New cursor y coordinate.
 */
void imui_set_cursor(imui_context_t *ui, const int16_t x, const int16_t y)
{
    if (!ui)
        return;

    ui->cursor_x     = x;
    ui->cursor_y     = y;
    ui->line_start_x = x;
    ui->line_start_y = y;
}

/**
 * @brief Continue layout on the same row after the previous item.
 *
 * @param ui UI context.
 * @param spacing Horizontal spacing, or negative to use style spacing.
 */
void imui_same_line(imui_context_t *ui, int16_t spacing)
{
    if (!ui)
        return;

    if (spacing < 0)
        spacing = (int16_t)ui->style.item_spacing_x;

    ui->cursor_x     = (int16_t)(ui->last_item_x + (int)ui->last_item_w + spacing);
    ui->cursor_y     = ui->last_item_y;
    ui->line_start_y = ui->last_item_y;
}

/**
 * @brief Draw a filled rectangle and mark it dirty.
 *
 * @param ui UI context.
 * @param x Rectangle x coordinate.
 * @param y Rectangle y coordinate.
 * @param width Rectangle width.
 * @param height Rectangle height.
 * @param color ARGB fill color.
 */
void imui_fill_rect(imui_context_t *ui, const int16_t x, const int16_t y, const uint16_t width, const uint16_t height,
                    const uint32_t color)
{
    if (!ui || !ui->context || width == 0 || height == 0)
        return;

    context_fill_rect(ui->context, x, y, width, height, color);
    imui_track_dirty(ui, x, y, width, height);
}

/**
 * @brief Draw text and mark its pixel bounds dirty.
 *
 * @param ui UI context.
 * @param x Text x coordinate.
 * @param y Text y coordinate.
 * @param text Null-terminated string.
 * @param color ARGB text color.
 */
void imui_text(imui_context_t *ui, const int16_t x, const int16_t y, const char *text, const uint32_t color)
{
    if (!ui || !ui->context || !text)
        return;

    context_draw_text(ui->context, text, x, y, color);

    const size_t len = strlen(text);
    if (len == 0)
        return;

    const int width = (int)len * VESA_CHAR_WIDTH;
    if (width <= 0)
        return;

    imui_track_dirty(ui, x, y, (uint16_t)width, VESA_CHAR_HEIGHT);
}

/**
 * @brief Draw a button and return true on click.
 *
 * @param ui UI context.
 * @param label Button label.
 * @param width Button width.
 * @param height Button height.
 * @return true if the button was clicked in this frame, false otherwise.
 */
bool imui_button(imui_context_t *ui, const char *label, const uint16_t width, const uint16_t height)
{
    if (!ui || !ui->context || !label || width == 0 || height == 0)
        return false;

    const int16_t x = ui->cursor_x;
    const int16_t y = ui->cursor_y;

    const uint32_t id  = ++ui->id_counter;
    const bool hovered = ui->mouse_inside &&
        ui->mouse_x >= x && ui->mouse_x < (x + (int)width) &&
        ui->mouse_y >= y && ui->mouse_y < (y + (int)height);

    if (hovered)
        ui->hot_id = id;

    const bool clicked = hovered && ui->mouse_pressed;
    if (clicked)
        ui->active_id = id;

    uint32_t fill_color = ui->style.button_color;
    if (ui->active_id == id && ui->mouse_down)
        fill_color = ui->style.button_active_color;
    else if (hovered)
        fill_color = ui->style.button_hot_color;

    context_fill_rect(ui->context, x, y, width, height, fill_color);
    context_draw_rect(ui->context, x, y, width, height, ui->style.button_border_color);

    const int text_width = (int)strlen(label) * VESA_CHAR_WIDTH;
    int text_x           = x + ((int)width - text_width) / 2;
    if (text_x < x + 2)
        text_x = x + 2;
    int text_y = y + ((int)height - VESA_CHAR_HEIGHT) / 2;
    if (text_y < y + 1)
        text_y = y + 1;
    context_draw_text(ui->context, label, text_x, text_y, ui->style.text_color);

    imui_track_dirty(ui, x, y, width, height);

    ui->last_item_x = x;
    ui->last_item_y = y;
    ui->last_item_w = width;
    ui->last_item_h = height;

    ui->cursor_x     = ui->line_start_x;
    ui->cursor_y     = (int16_t)(y + (int)height + ui->style.item_spacing_y);
    ui->line_start_y = ui->cursor_y;

    return clicked;
}
