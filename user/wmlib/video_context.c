#include <wm/video_context.h>
#include <wm/rect.h>
#include <wm/font.h>
#include <array.h>
#include <stdlib.h>

static inline uint8_t reverse_bits8(uint8_t v)
{
    v = (uint8_t)(((v & 0xF0u) >> 4) | ((v & 0x0Fu) << 4));
    v = (uint8_t)(((v & 0xCCu) >> 2) | ((v & 0x33u) << 2));
    v = (uint8_t)(((v & 0xAAu) >> 1) | ((v & 0x55u) << 1));
    return v;
}

static inline void framebuffer_fill_span32(uint8_t *dst, uint32_t pixel_count, uint32_t color)
{
    while (pixel_count--) {
        *((uint32_t *)dst) = color;
        dst                += 4;
    }
}

static inline void framebuffer_copy_span32(uint8_t *dst, const uint32_t *src, uint32_t pixel_count)
{
    while (pixel_count--) {
        *((uint32_t *)dst) = *src++;
        dst                += 4;
    }
}

static void framebuffer_fill_rect32(const video_context_t *context, int x, int y, int width, int height, uint32_t color)
{
    if (width <= 0 || height <= 0) {
        return;
    }

    int screen_w   = (int)context->width;
    int screen_h   = (int)context->height;
    uint32_t pitch = context->pitch;

    int x0 = x;
    int y0 = y;
    int x1 = x + width;
    int y1 = y + height;

    if (x1 <= 0 || y1 <= 0 || x0 >= screen_w || y0 >= screen_h) {
        return;
    }

    if (x0 < 0) {
        x0 = 0;
    }
    if (y0 < 0) {
        y0 = 0;
    }
    if (x1 > screen_w) {
        x1 = screen_w;
    }
    if (y1 > screen_h) {
        y1 = screen_h;
    }

    uint32_t span_pixels = (uint32_t)(x1 - x0);
    uint8_t *row         = (uint8_t *)context->buffer + (uint32_t)y0 * pitch + (uint32_t)x0 * 4U;

    for (int j = y0; j < y1; j++) {
        framebuffer_fill_span32(row, span_pixels, color);
        row += pitch;
    }
}

static void framebuffer_blit_span32(const video_context_t *context, int x, int y, const uint32_t *src,
                                    uint32_t pixel_count)
{
    if (!src || pixel_count == 0) {
        return;
    }

    int screen_w = (int)context->width;
    int screen_h = (int)context->height;

    if (y < 0 || y >= screen_h) {
        return;
    }

    int x0          = x;
    uint32_t offset = 0;
    if (x0 < 0) {
        offset = (uint32_t)(-x0);
        if (offset >= pixel_count) {
            return;
        }
        pixel_count -= offset;
        x0          = 0;
    }

    if (x0 >= screen_w) {
        return;
    }

    if (x0 + (int)pixel_count > screen_w) {
        pixel_count = (uint32_t)(screen_w - x0);
    }

    uint32_t pitch = context->pitch;
    uint8_t *row   = (uint8_t *)context->buffer + (uint32_t)y * pitch + (uint32_t)x0 * 4U;
    framebuffer_copy_span32(row, src + offset, pixel_count);
}

video_context_t *context_new(uint32_t *buffer, uint16_t width, uint16_t height, uint32_t pitch)
{
    auto context = (video_context_t *)malloc(sizeof(video_context_t));
    if (!context) {
        return nullptr;
    }

    context->buffer      = buffer;
    context->width       = width;
    context->height      = height;
    context->pitch       = pitch;
    context->translate_x = 0;
    context->translate_y = 0;
    context->clip_rects  = nullptr;
    context->clipping_on = 0;

    return context;
}

static void context_clipped_rect_bitmap(const video_context_t *context, int x, int y, unsigned int draw_width,
                                        unsigned int draw_height, const rect_t *clip_area, const uint32_t *pixels,
                                        unsigned int stride, int src_origin_x, int src_origin_y)
{
    int max_x = x + (int)draw_width;
    int max_y = y + (int)draw_height;

    const int draw_origin_x = x + context->translate_x;
    const int draw_origin_y = y + context->translate_y;
    x                       = draw_origin_x;
    y                       = draw_origin_y;
    max_x                   += context->translate_x;
    max_y                   += context->translate_y;

    if (x < clip_area->left) {
        x = clip_area->left;
    }

    if (y < clip_area->top) {
        y = clip_area->top;
    }

    if (max_x > clip_area->right + 1) {
        max_x = clip_area->right + 1;
    }

    if (max_y > clip_area->bottom + 1) {
        max_y = clip_area->bottom + 1;
    }

    if (x >= max_x || y >= max_y) {
        return;
    }

    const int src_base_x = src_origin_x + (x - draw_origin_x);
    const int src_base_y = src_origin_y + (y - draw_origin_y);

    const int span = max_x - x;
    if (span <= 0) {
        return;
    }

    for (int draw_y = y; draw_y < max_y; draw_y++) {
        const int src_y         = src_base_y + (draw_y - y);
        const uint32_t *src_row = pixels + (uint32_t)src_y * stride + (uint32_t)src_base_x;
        framebuffer_blit_span32(context, x, draw_y, src_row, (uint32_t)span);
    }
}

static void context_clipped_rect(const video_context_t *context, int x, int y, unsigned int width, unsigned int height,
                                 const rect_t *clip_area, uint32_t color)
{
    int max_x = x + (int)width;
    int max_y = y + (int)height;

    x     += context->translate_x;
    y     += context->translate_y;
    max_x += context->translate_x;
    max_y += context->translate_y;

    if (x < clip_area->left) {
        x = clip_area->left;
    }

    if (y < clip_area->top) {
        y = clip_area->top;
    }

    if (max_x > clip_area->right + 1) {
        max_x = clip_area->right + 1;
    }

    if (max_y > clip_area->bottom + 1) {
        max_y = clip_area->bottom + 1;
    }

    if (x >= max_x || y >= max_y) {
        return;
    }

    const int width_span  = max_x - x;
    const int height_span = max_y - y;
    framebuffer_fill_rect32(context, x, y, width_span, height_span, color);
}

void context_draw_bitmap(const video_context_t *context, int x, int y, unsigned int width, unsigned int height,
                         uint32_t *pixels)
{
    if (!context || !pixels || width == 0 || height == 0) {
        return;
    }

    const unsigned int stride = width;
    int draw_x                = x;
    int draw_y                = y;
    unsigned int draw_width   = width;
    unsigned int draw_height  = height;
    int src_origin_x          = 0;
    int src_origin_y          = 0;

    if (draw_x < 0) {
        src_origin_x = -draw_x;
        if (src_origin_x >= (int)draw_width) {
            return;
        }
        draw_width -= (unsigned int)src_origin_x;
        draw_x     = 0;
    }

    if (draw_y < 0) {
        src_origin_y = -draw_y;
        if (src_origin_y >= (int)draw_height) {
            return;
        }
        draw_height -= (unsigned int)src_origin_y;
        draw_y      = 0;
    }

    if (draw_x + (int)draw_width > context->width) {
        const int overflow = draw_x + (int)draw_width - context->width;
        if (overflow >= (int)draw_width) {
            return;
        }
        draw_width -= (unsigned int)overflow;
    }

    if (draw_y + (int)draw_height > context->height) {
        const int overflow = draw_y + (int)draw_height - context->height;
        if (overflow >= (int)draw_height) {
            return;
        }
        draw_height -= (unsigned int)overflow;
    }

    if (draw_width == 0 || draw_height == 0) {
        return;
    }

    rect_t screen_area;

    size_t clip_count = arr_len(context->clip_rects);
    if (clip_count) {
        for (size_t i = 0; i < clip_count; i++) {
            rect_t *clip_area = arr_get(context->clip_rects, i);
            if (!clip_area) {
                continue;
            }
            context_clipped_rect_bitmap(context,
                                        draw_x,
                                        draw_y,
                                        draw_width,
                                        draw_height,
                                        clip_area,
                                        pixels,
                                        stride,
                                        src_origin_x,
                                        src_origin_y);
        }
    } else {
        if (!context->clipping_on) {
            screen_area.top    = 0;
            screen_area.left   = 0;
            screen_area.bottom = context->height - 1;
            screen_area.right  = context->width - 1;
            context_clipped_rect_bitmap(context,
                                        draw_x,
                                        draw_y,
                                        draw_width,
                                        draw_height,
                                        &screen_area,
                                        pixels,
                                        stride,
                                        src_origin_x,
                                        src_origin_y);
        }
    }
}

void context_fill_rect(const video_context_t *context, int x, int y, unsigned int width, unsigned int height,
                       uint32_t color)
{
    if (!context) {
        return;
    }

    int max_x = x + (int)width;
    int max_y = y + (int)height;
    rect_t screen_area;

    if (max_x > context->width) {
        max_x = context->width;
    }

    if (max_y > context->height) {
        max_y = context->height;
    }

    if (x < 0) {
        x = 0;
    }

    if (y < 0) {
        y = 0;
    }

    width  = max_x - x;
    height = max_y - y;

    size_t clip_count = arr_len(context->clip_rects);
    if (clip_count) {
        for (size_t i = 0; i < clip_count; i++) {
            rect_t *clip_area = arr_get(context->clip_rects, i);
            if (!clip_area) {
                continue;
            }
            context_clipped_rect(context, x, y, width, height, clip_area, color);
        }
    } else {
        if (!context->clipping_on) {
            screen_area.top    = 0;
            screen_area.left   = 0;
            screen_area.bottom = context->height - 1;
            screen_area.right  = context->width - 1;
            context_clipped_rect(context, x, y, width, height, &screen_area, color);
        }
    }
}

void context_horizontal_line(const video_context_t *context, int x, int y, unsigned int length, uint32_t color)
{
    context_fill_rect(context, x, y, length, 1, color);
}

void context_vertical_line(const video_context_t *context, int x, int y, unsigned int length, uint32_t color)
{
    context_fill_rect(context, x, y, 1, length, color);
}

void context_draw_rect(const video_context_t *context, int x, int y, unsigned int width, unsigned int height,
                       uint32_t color)
{
    context_horizontal_line(context, x, y, width, color);
    context_vertical_line(context, x, y + 1, height - 2, color);
    context_horizontal_line(context, x, y + (int)height - 1, width, color);
    context_vertical_line(context, x + (int)width - 1, y + 1, height - 2, color);
}

void context_intersect_clip_rect(video_context_t *context, rect_t *rect)
{
    if (!context) {
        free(rect);
        return;
    }

    if (!rect) {
        return;
    }

    context->clipping_on = 1;

    rect_t **output_rects = nullptr;

    size_t clip_count = arr_len(context->clip_rects);
    for (size_t i = 0; i < clip_count; i++) {
        rect_t *current_rect = arr_get(context->clip_rects, i);
        if (!current_rect) {
            continue;
        }

        rect_t *intersect_rect = rect_intersect(current_rect, rect);

        if (intersect_rect && !arr_try_push(output_rects, intersect_rect))
            free(intersect_rect);
    }

    for (size_t i = 0; i < clip_count; i++) {
        free(context->clip_rects[i]);
    }
    arr_free(context->clip_rects);

    context->clip_rects = output_rects;

    free(rect);
}

void context_subtract_clip_rect(video_context_t *context, rect_t *subtracted_rect)
{
    if (!context || !subtracted_rect) {
        return;
    }

    context->clipping_on = 1;

    for (size_t i = 0; i < arr_len(context->clip_rects);) {
        rect_t *cur_rect = arr_get(context->clip_rects, i);
        if (!cur_rect) {
            i++;
            continue;
        }

        if (!(cur_rect->left <= subtracted_rect->right && cur_rect->right >= subtracted_rect->left &&
            cur_rect->top <= subtracted_rect->bottom && cur_rect->bottom >= subtracted_rect->top)) {
            i++;
            continue;
        }

        arr_remove_at(context->clip_rects, i);
        rect_t **split_rects = rect_split(cur_rect, subtracted_rect);
        if (!split_rects) {
            if (!arr_try_push(context->clip_rects, cur_rect))
                free(cur_rect);
            return;
        }
        free(cur_rect);

        size_t split_count = arr_len(split_rects);
        for (size_t split_idx = 0; split_idx < split_count; split_idx++) {
            cur_rect = split_rects[split_idx];
            if (!cur_rect) {
                continue;
            }
            if (!arr_try_push(context->clip_rects, cur_rect))
                free(cur_rect);
        }

        arr_free(split_rects);

        i = 0;
    }
}

void context_add_clip_rect(video_context_t *context, rect_t *added_rect)
{
    if (!context || !added_rect) {
        free(added_rect);
        return;
    }

    context_subtract_clip_rect(context, added_rect);
    if (!arr_try_push(context->clip_rects, added_rect))
        free(added_rect);
}

void context_clear_clip_rects(video_context_t *context)
{
    if (!context) {
        return;
    }

    context->clipping_on = 0;

    size_t clip_count = arr_len(context->clip_rects);
    for (size_t i = 0; i < clip_count; i++) {
        free(context->clip_rects[i]);
    }
    arr_clear(context->clip_rects);
}

static void context_draw_char_clipped(const video_context_t *context, char character, int x, int y, uint32_t color,
                                      const rect_t *bound_rect)
{
    int off_x   = 0;
    int off_y   = 0;
    int count_x = VESA_CHAR_WIDTH;
    int count_y = VESA_CHAR_HEIGHT;

    x += context->translate_x;
    y += context->translate_y;

    int clip_left   = bound_rect->left;
    int clip_top    = bound_rect->top;
    int clip_right  = bound_rect->right;
    int clip_bottom = bound_rect->bottom;

    const int max_x = (int)context->width - 1;
    const int max_y = (int)context->height - 1;
    if (clip_left < 0)
        clip_left = 0;
    if (clip_top < 0)
        clip_top = 0;
    if (clip_right > max_x)
        clip_right = max_x;
    if (clip_bottom > max_y)
        clip_bottom = max_y;
    if (clip_left > clip_right || clip_top > clip_bottom) {
        return;
    }

    character &= 0x7F;

    if (x > clip_right || (x + VESA_CHAR_WIDTH) <= clip_left || y > clip_bottom || (y + VESA_CHAR_HEIGHT) <= clip_top) {
        return;
    }

    if (x < clip_left) {
        off_x = clip_left - x;
    }

    if ((x + VESA_CHAR_WIDTH) > clip_right) {
        count_x = clip_right - x + 1;
    }

    if (y < clip_top) {
        off_y = clip_top - y;
    }

    if ((y + VESA_CHAR_HEIGHT) > clip_bottom) {
        count_y = clip_bottom - y + 1;
    }

    uint32_t pitch_bytes = context->pitch;
    const int dst_x      = x + off_x;

    for (int font_y = off_y; font_y < count_y; font_y++) {
        uint8_t row_bits  = reverse_bits8(font8x16[font_y * 128 + character]);
        uint32_t row_mask = 0xFFu;
        if (off_x > 0) {
            row_mask &= ~((1u << off_x) - 1u);
        }
        if (count_x < VESA_CHAR_WIDTH) {
            row_mask &= (count_x == 0 ? 0u : ((1u << count_x) - 1u));
        }
        uint8_t active = (uint8_t)(row_bits & row_mask);
        if (active == 0) {
            continue;
        }
        uint32_t *dst = (uint32_t *)((uint8_t *)context->buffer + (uint32_t)(y + font_y) * pitch_bytes + (uint32_t)dst_x
            * 4U);
        while (active) {
            int bit          = __builtin_ctz(active);
            dst[bit - off_x] = color;
            active           &= active - 1;
        }
    }
}

void context_draw_char(const video_context_t *context, char character, int x, int y, uint32_t color)
{
    if (!context) {
        return;
    }

    size_t clip_count = arr_len(context->clip_rects);
    if (clip_count) {
        for (size_t i = 0; i < clip_count; i++) {
            rect_t *clip_area = arr_get(context->clip_rects, i);
            if (!clip_area) {
                continue;
            }
            context_draw_char_clipped(context, character, x, y, color, clip_area);
        }
    } else {
        if (!context->clipping_on) {
            rect_t screen_area;

            screen_area.top    = 0;
            screen_area.left   = 0;
            screen_area.bottom = context->height - 1;
            screen_area.right  = context->width - 1;
            context_draw_char_clipped(context, character, x, y, color, &screen_area);
        }
    }
}

void context_draw_text(const video_context_t *context, const char *string, int x, int y, uint32_t color)
{
    for (; *string; x += VESA_CHAR_WIDTH)
        context_draw_char(context, *(string++), x, y, color);
}
