#pragma once
#include <stdint.h>
#include <wm/wmclient.h>
#include <wm/video_context.h>

struct client_button;

typedef void (*ButtonMousedownHandler)(const struct client_button *, int, int);
typedef void (*ButtonPaintFunction)(const struct client_button *);

typedef struct client_button
{
    wm_window_t *window;
    video_context_t *context;
    uint8_t color_toggle;
    ButtonMousedownHandler onmousedown;
    ButtonPaintFunction paint;
} client_button_t;

client_button_t *client_button_new(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t parent_id, const char *title);

// void client_button_mousedown_handler(const window_t *button_window, int16_t x, int16_t y);
// void client_button_paint(const window_t *button_window);