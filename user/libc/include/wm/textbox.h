#pragma once
#include <wm/window.h>
#include <stdint.h>

typedef struct textbox {
    window_t window;
} textbox_t;

textbox_t *textbox_new(int16_t x, int16_t y, int width, int height);
void textbox_paint(const window_t *text_box_window);
