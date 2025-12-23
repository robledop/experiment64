#include <wm/button.h>
#include <wm/desktop.h>
#include <stdlib.h>
#include <string.h>

button_t *button_new(int16_t x, int16_t y, int16_t w, int16_t h)
{
    button_t *button = (button_t *)malloc(sizeof(button_t));
    if (!button) {
        return button;
    }

    if (!window_init((window_t *)button, x, y, w, h, WIN_NODECORATION, nullptr)) {

        free(button);
        return nullptr;
    }

    button->window.paint_function = button_paint;
    button->window.mousedown_function = button_mousedown_handler;

    button->onmousedown = nullptr;

    button->color_toggle = 0;

    return button;
}

void button_paint(window_t *button_window)
{
    button_t *button = (button_t *)button_window;

    uint32_t border_color;
    if (button->color_toggle) {
        border_color = WIN_TITLE_COLOR;
    } else {
        border_color = WIN_BGCOLOR - 0x101010;
    }

    context_fill_rect(button_window->context, 1, 1, button_window->width - 1, button_window->height - 1, WIN_BGCOLOR);
    context_draw_rect(button_window->context, 0, 0, button_window->width, button_window->height, 0xFF000000);
    context_draw_rect(button_window->context, 3, 3, button_window->width - 6, button_window->height - 6, border_color);
    context_draw_rect(button_window->context, 4, 4, button_window->width - 8, button_window->height - 8, border_color);

    int title_len = button_window->title ? (int)strlen(button_window->title) : 0;

    title_len *= VESA_CHAR_WIDTH;

    if (button_window->title) {
        context_draw_text(button_window->context,
                          button_window->title,
                          (button_window->width / 2) - (title_len / 2),
                          (button_window->height / 2) - 6,
                          WIN_TEXT_COLOR);
    }
}

void button_mousedown_handler(window_t *button_window, int16_t x, int16_t y)
{
    button_t *button = (button_t *)button_window;

    (void)x;
    (void)y;

    window_invalidate((window_t *)button, 0, 0, button->window.height - 1, button->window.width - 1);

    if (button->onmousedown) {
        button->onmousedown(button, x, y);
    }
}

