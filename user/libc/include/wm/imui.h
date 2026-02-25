#pragma once

#include <stdint.h>
#include <wm/video_context.h>
#include <wm/wm_protocol.h>
#include <wm/wmclient.h>

typedef struct
{
    uint32_t background_color;
    uint32_t button_color;
    uint32_t button_hot_color;
    uint32_t button_active_color;
    uint32_t button_border_color;
    uint32_t text_color;
    uint16_t item_spacing_x;
    uint16_t item_spacing_y;
} imui_style_t;

typedef struct
{
    wm_window_t *window;
    video_context_t *context;
    imui_style_t style;
    int16_t cursor_x;
    int16_t cursor_y;
    int16_t line_start_x;
    int16_t line_start_y;
    uint16_t line_height;
    int16_t last_item_x;
    int16_t last_item_y;
    uint16_t last_item_w;
    uint16_t last_item_h;
    int16_t mouse_x;
    int16_t mouse_y;
    bool mouse_pressed;
    bool mouse_down;
    bool mouse_inside;
    uint32_t id_counter;
    uint32_t hot_id;
    uint32_t active_id;
    bool has_dirty;
    int16_t dirty_left;
    int16_t dirty_top;
    int16_t dirty_right;
    int16_t dirty_bottom;
} imui_context_t;

bool imui_init(imui_context_t *ui, wm_window_t *window);
void imui_deinit(imui_context_t *ui);
void imui_begin_frame(imui_context_t *ui, const wm_event_mouse_t *mouse_event);
void imui_end_frame(imui_context_t *ui);
void imui_set_cursor(imui_context_t *ui, int16_t x, int16_t y);
void imui_same_line(imui_context_t *ui, int16_t spacing);
void imui_fill_rect(imui_context_t *ui, int16_t x, int16_t y, uint16_t width, uint16_t height, uint32_t color);
void imui_text(imui_context_t *ui, int16_t x, int16_t y, const char *text, uint32_t color);
bool imui_button(imui_context_t *ui, const char *label, uint16_t width, uint16_t height);
