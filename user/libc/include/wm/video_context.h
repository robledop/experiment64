#pragma once

#include <wm/rect.h>
#include <stdint.h>

#define VESA_CHAR_WIDTH 8
#define VESA_CHAR_HEIGHT 12
#define VESA_LINE_HEIGHT 14

typedef struct video_context
{
    uint32_t *buffer;
    uint16_t width;
    uint16_t height;
    uint32_t pitch;
    int translate_x;
    int translate_y;
    rect_t **clip_rects;
    uint8_t clipping_on;
} video_context_t;

video_context_t *context_new(uint32_t *buffer, uint16_t width, uint16_t height, uint32_t pitch);
void context_fill_rect(const video_context_t *context, int x, int y, unsigned int width, unsigned int height, uint32_t color);
void context_draw_bitmap(const video_context_t *context, int x, int y, unsigned int width, unsigned int height, uint32_t *pixels);
void context_horizontal_line(const video_context_t *context, int x, int y, unsigned int length, uint32_t color);
void context_vertical_line(const video_context_t *context, int x, int y, unsigned int length, uint32_t color);
void context_draw_rect(const video_context_t *context, int x, int y, unsigned int width, unsigned int height, uint32_t color);
void context_intersect_clip_rect(video_context_t *context, rect_t *rect);
void context_subtract_clip_rect(video_context_t *context, rect_t *subtracted_rect);
void context_add_clip_rect(video_context_t *context, rect_t *rect);
void context_clear_clip_rects(video_context_t *context);
void context_draw_char(const video_context_t *context, char character, int x, int y, uint32_t color);
void context_draw_text(const video_context_t *context, const char *string, int x, int y, uint32_t color);
