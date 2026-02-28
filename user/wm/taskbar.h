#pragma once
#include <wm/button.h>
#include <wm/window.h>

typedef struct taskbar_button {
    button_t *button;
    window_t *window;
} taskbar_button_t;

void taskbar_init(int16_t x, int16_t y, uint16_t w, uint16_t h);
window_t *taskbar_get();
void taskbar_remove_button(int idx);
void taskbar_add_button(const char *title, window_t *window);
void taskbar_button_activate(int index);
int taskbar_find_button(const button_t *button);
int taskbar_find_window(const window_t *window);
