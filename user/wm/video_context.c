#include <wm/video_context.h>
#include <wm/rect.h>
#include <wm/font.h>
#include <stdlib.h>
#include <string.h>

static inline uint8_t reverse_bits8(uint8_t v)
{
    v = (uint8_t)(((v & 0xF0u) >> 4) | ((v & 0x0Fu) << 4));
    v = (uint8_t)(((v & 0xCCu) >> 2) | ((v & 0x33u) << 2));
    v = (uint8_t)(((v & 0xAAu) >> 1) | ((v & 0x55u) << 1));
    return v;
}

__attribute__((target("sse2,avx")))
static inline void framebuffer_fill_span32(uint8_t *dst, uint32_t pixel_count, uint32_t color)
{
    while (pixel_count--) {
        *((uint32_t *)dst) = color;
        dst += 4;
    }
}

__attribute__((target("sse2,avx")))
static inline void framebuffer_copy_span32(uint8_t *dst, const uint32_t *src, uint32_t pixel_count)
{
    while (pixel_count--) {
        *((uint32_t *)dst) = *src++;
        dst += 4;
    }
}

__attribute__((target("sse2,avx")))
static void framebuffer_fill_rect32(video_context_t *context, int x, int y, int width, int height, uint32_t color)
{
    if (width <= 0 || height <= 0) {
        return;
    }

    int screen_w = (int)context->width;
    int screen_h = (int)context->height;
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
    uint8_t *row = (uint8_t *)context->buffer + (uint32_t)y0 * pitch + (uint32_t)x0 * 4U;

    for (int j = y0; j < y1; j++) {
        framebuffer_fill_span32(row, span_pixels, color);
        row += pitch;
    }
}

__attribute__((target("sse2,avx")))
static void framebuffer_blit_span32(video_context_t *context, int x, int y, const uint32_t *src, uint32_t pixel_count)
{
    if (!src || pixel_count == 0) {
        return;
    }

    int screen_w = (int)context->width;
    int screen_h = (int)context->height;

    if (y < 0 || y >= screen_h) {
        return;
    }

    int x0 = x;
    uint32_t offset = 0;
    if (x0 < 0) {
        offset = (uint32_t)(-x0);
        if (offset >= pixel_count) {
            return;
        }
        pixel_count -= offset;
        x0 = 0;
    }

    if (x0 >= screen_w) {
        return;
    }

    if (x0 + (int)pixel_count > screen_w) {
        pixel_count = (uint32_t)(screen_w - x0);
    }

    uint32_t pitch = context->pitch;
    uint8_t *row = (uint8_t *)context->buffer + (uint32_t)y * pitch + (uint32_t)x0 * 4U;
    framebuffer_copy_span32(row, src + offset, pixel_count);
}

video_context_t *context_new(uint32_t *fb, uint16_t width, uint16_t height, uint32_t pitch)
{
    video_context_t *context = (video_context_t *)malloc(sizeof(video_context_t));
    if (!context) {
        return nullptr;
    }

    context->clip_rects = list_new();
    if (!context->clip_rects) {
        free(context);
        return nullptr;
    }

    context->buffer = fb;
    context->width = width;
    context->height = height;
    context->pitch = pitch;
    context->translate_x = 0;
    context->translate_y = 0;
    context->clipping_on = 0;

    return context;
}

__attribute__((target("sse2,avx")))
static void context_clipped_rect_bitmap(video_context_t *context, int x, int y, unsigned int draw_width,
                                 unsigned int draw_height, rect_t *clip_area, const uint32_t *pixels,
                                 unsigned int stride, int src_origin_x, int src_origin_y)
{
    int max_x = x + (int)draw_width;
    int max_y = y + (int)draw_height;

    const int draw_origin_x = x + context->translate_x;
    const int draw_origin_y = y + context->translate_y;
    x = draw_origin_x;
    y = draw_origin_y;
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

    const int src_base_x = src_origin_x + (x - draw_origin_x);
    const int src_base_y = src_origin_y + (y - draw_origin_y);

    const int span = max_x - x;
    if (span <= 0) {
        return;
    }

    for (int draw_y = y; draw_y < max_y; draw_y++) {
        const int src_y = src_base_y + (draw_y - y);
        const uint32_t *src_row = pixels + (uint32_t)src_y * stride + (uint32_t)src_base_x;
        framebuffer_blit_span32(context, x, draw_y, src_row, (uint32_t)span);
    }
}

static void context_clipped_rect(video_context_t *context, int x, int y, unsigned int width, unsigned int height,
                          rect_t *clip_area, uint32_t color)
{
    int max_x = x + (int)width;
    int max_y = y + (int)height;

    x += context->translate_x;
    y += context->translate_y;
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

    const int width_span = max_x - x;
    const int height_span = max_y - y;
    framebuffer_fill_rect32(context, x, y, width_span, height_span, color);
}

__attribute__((target("sse2,avx")))
void context_draw_bitmap(video_context_t *context, int x, int y, unsigned int width, unsigned int height,
                         uint32_t *pixels)
{
    if (!pixels || width == 0 || height == 0) {
        return;
    }

    const unsigned int stride = width;
    int draw_x = x;
    int draw_y = y;
    unsigned int draw_width = width;
    unsigned int draw_height = height;
    int src_origin_x = 0;
    int src_origin_y = 0;

    if (draw_x < 0) {
        src_origin_x = -draw_x;
        if (src_origin_x >= (int)draw_width) {
            return;
        }
        draw_width -= (unsigned int)src_origin_x;
        draw_x = 0;
    }

    if (draw_y < 0) {
        src_origin_y = -draw_y;
        if (src_origin_y >= (int)draw_height) {
            return;
        }
        draw_height -= (unsigned int)src_origin_y;
        draw_y = 0;
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

    if (context->clip_rects->count) {
        for (unsigned int i = 0; i < context->clip_rects->count; i++) {
            rect_t *clip_area = (rect_t *)list_get_at(context->clip_rects, i);
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
            screen_area.top = 0;
            screen_area.left = 0;
            screen_area.bottom = context->height - 1;
            screen_area.right = context->width - 1;
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

void context_fill_rect(video_context_t *context, int x, int y, unsigned int width, unsigned int height, uint32_t color)
{
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

    width = max_x - x;
    height = max_y - y;

    if (context->clip_rects->count) {
        for (unsigned int i = 0; i < context->clip_rects->count; i++) {
            rect_t *clip_area = (rect_t *)list_get_at(context->clip_rects, i);
            context_clipped_rect(context, x, y, width, height, clip_area, color);
        }
    } else {
        if (!context->clipping_on) {
            screen_area.top = 0;
            screen_area.left = 0;
            screen_area.bottom = context->height - 1;
            screen_area.right = context->width - 1;
            context_clipped_rect(context, x, y, width, height, &screen_area, color);
        }
    }
}

void context_horizontal_line(video_context_t *context, int x, int y, unsigned int length, uint32_t color)
{
    context_fill_rect(context, x, y, length, 1, color);
}

void context_vertical_line(video_context_t *context, int x, int y, unsigned int length, uint32_t color)
{
    context_fill_rect(context, x, y, 1, length, color);
}

void context_draw_rect(video_context_t *context, int x, int y, unsigned int width, unsigned int height, uint32_t color)
{
    context_horizontal_line(context, x, y, width, color);
    context_vertical_line(context, x, y + 1, height - 2, color);
    context_horizontal_line(context, x, y + (int)height - 1, width, color);
    context_vertical_line(context, x + (int)width - 1, y + 1, height - 2, color);
}

__attribute__((target("sse2,avx")))
void context_intersect_clip_rect(video_context_t *context, rect_t *rect)
{
    context->clipping_on = 1;

    list_t *output_rects = list_new();
    if (!output_rects) {
        return;
    }

    for (unsigned int i = 0; i < context->clip_rects->count; i++) {
        rect_t *current_rect = (rect_t *)list_get_at(context->clip_rects, i);
        rect_t *intersect_rect = rect_intersect(current_rect, rect);

        if (intersect_rect) {
            list_add(output_rects, intersect_rect);
        }
    }

    while (context->clip_rects->count) {
        free(list_remove_at(context->clip_rects, 0));
    }
    free(context->clip_rects);

    context->clip_rects = output_rects;

    free(rect);
}

__attribute__((target("sse2,avx")))
void context_subtract_clip_rect(video_context_t *context, rect_t *subtracted_rect)
{
    context->clipping_on = 1;

    for (unsigned int i = 0; i < context->clip_rects->count;) {
        rect_t *cur_rect = list_get_at(context->clip_rects, i);

        if (!(cur_rect->left <= subtracted_rect->right && cur_rect->right >= subtracted_rect->left &&
            cur_rect->top <= subtracted_rect->bottom && cur_rect->bottom >= subtracted_rect->top)) {
            i++;
            continue;
        }

        list_remove_at(context->clip_rects, i);
        list_t *split_rects = rect_split(cur_rect, subtracted_rect);
        free(cur_rect);

        while (split_rects->count) {
            cur_rect = (rect_t *)list_remove_at(split_rects, 0);
            list_add(context->clip_rects, cur_rect);
        }

        free(split_rects);

        i = 0;
    }
}

void context_add_clip_rect(video_context_t *context, rect_t *added_rect)
{
    context_subtract_clip_rect(context, added_rect);
    list_add(context->clip_rects, added_rect);
}

void context_clear_clip_rects(video_context_t *context)
{
    context->clipping_on = 0;

    while (context->clip_rects->count) {
        rect_t *cur_rect = (rect_t *)list_remove_at(context->clip_rects, 0);
        free(cur_rect);
    }
}

__attribute__((target("sse2,avx")))
static void context_draw_char_clipped(video_context_t *context, char character, int x, int y, uint32_t color,
                               rect_t *bound_rect)
{
    int off_x = 0;
    int off_y = 0;
    int count_x = VESA_CHAR_WIDTH;
    int count_y = VESA_CHAR_HEIGHT;

    x += context->translate_x;
    y += context->translate_y;

    character &= 0x7F;

    if (x > bound_rect->right || (x + 8) <= bound_rect->left || y > bound_rect->bottom || (y + 12) <= bound_rect->top) {
        return;
    }

    if (x < bound_rect->left) {
        off_x = bound_rect->left - x;
    }

    if ((x + 8) > bound_rect->right) {
        count_x = bound_rect->right - x + 1;
    }

    if (y < bound_rect->top) {
        off_y = bound_rect->top - y;
    }

    if ((y + 12) > bound_rect->bottom) {
        count_y = bound_rect->bottom - y + 1;
    }

    uint32_t pitch_bytes = context->pitch;

    for (int font_y = off_y; font_y < count_y; font_y++) {
        uint8_t row_bits = reverse_bits8(font8x12[font_y * 128 + character]);
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
        uint32_t *dst = (uint32_t *)((uint8_t *)context->buffer + (uint32_t)(y + font_y) * pitch_bytes + (uint32_t)x * 4U);
        while (active) {
            int bit = __builtin_ctz(active);
            dst[bit] = color;
            active &= active - 1;
        }
    }
}

void context_draw_char(video_context_t *context, char character, int x, int y, uint32_t color)
{
    if (context->clip_rects->count) {
        for (unsigned int i = 0; i < context->clip_rects->count; i++) {
            rect_t *clip_area = (rect_t *)list_get_at(context->clip_rects, i);
            context_draw_char_clipped(context, character, x, y, color, clip_area);
        }
    } else {
        if (!context->clipping_on) {
            rect_t screen_area;

            screen_area.top = 0;
            screen_area.left = 0;
            screen_area.bottom = context->height - 1;
            screen_area.right = context->width - 1;
            context_draw_char_clipped(context, character, x, y, color, &screen_area);
        }
    }
}

void context_draw_text(video_context_t *context, char *string, int x, int y, uint32_t color)
{
    for (; *string; x += VESA_CHAR_WIDTH)
        context_draw_char(context, *(string++), x, y, color);
}

