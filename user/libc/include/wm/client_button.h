#pragma once
#include <wm/window.h>
#include <stdint.h>

struct button;

typedef void (*ButtonMousedownHandler)(const struct button *, int, int);

typedef struct button {
    window_t window;
    uint8_t color_toggle;
    ButtonMousedownHandler onmousedown;
} button_t;

button_t *client_button_new(int16_t x, int16_t y, int16_t w, int16_t h);
void client_button_mousedown_handler(const window_t *button_window, int16_t x, int16_t y);
void client_button_paint(const window_t *button_window);

