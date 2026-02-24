#include <wm/client_button.h>
#include <stdlib.h>
#include <wm/wmclient.h>
#include <string.h>

void client_button_paint(const client_button_t *button)
{
    uint32_t border_color;
    if (button->color_toggle) {
        border_color = WIN_TITLE_COLOR;
    } else {
        border_color = WIN_BGCOLOR - 0x101010;
    }

    context_fill_rect(button->context, 1, 1, button->window->width - 1, button->window->height - 1, WIN_BGCOLOR);
    context_draw_rect(button->context, 0, 0, button->window->width, button->window->height, 0xFF000000);
    context_draw_rect(button->context, 3, 3, button->window->width - 6, button->window->height - 6, border_color);
    context_draw_rect(button->context, 4, 4, button->window->width - 8, button->window->height - 8, border_color);

    int title_len = (int)strlen(button->window->title);

    title_len *= VESA_CHAR_WIDTH;

    context_draw_text(button->context,
                      button->window->title,
                      (button->window->width / 2) - (title_len / 2),
                      (button->window->height / 2) - 6,
                      WIN_TEXT_COLOR);
}

client_button_t *client_button_new(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t parent_id, const char *title)
{
    auto button = (client_button_t *)malloc(sizeof(client_button_t));
    if (!button) {
        return button;
    }

    wm_window_t *window = wm_create_window(x, y, w, h, WIN_NODECORATION, parent_id, title);
    if (!window) {
        free(button);
        return nullptr;
    }

    video_context_t *button_context = context_new(window->buffer, window->width, window->height, window->width * 4);
    if (!button_context) {
        wm_destroy_window(window);
    }

    button->window       = window;
    button->onmousedown  = nullptr;
    button->color_toggle = 0;
    button->context      = button_context;
    button->paint        = client_button_paint;

    return button;
}

// void client_button_paint(const window_t *button_window)
// {
//     button_t *button = (button_t *)button_window;
//
//     uint32_t border_color;
//     if (button->color_toggle) {
//         border_color = WIN_TITLE_COLOR;
//     } else {
//         border_color = WIN_BGCOLOR - 0x101010;
//     }
//
//     context_fill_rect(button_window->context, 1, 1, button_window->width - 1, button_window->height - 1, WIN_BGCOLOR);
//     context_draw_rect(button_window->context, 0, 0, button_window->width, button_window->height, 0xFF000000);
//     context_draw_rect(button_window->context, 3, 3, button_window->width - 6, button_window->height - 6, border_color);
//     context_draw_rect(button_window->context, 4, 4, button_window->width - 8, button_window->height - 8, border_color);
//
//     int title_len = button_window->title ? (int)strlen(button_window->title) : 0;
//
//     title_len *= VESA_CHAR_WIDTH;
//
//     if (button_window->title) {
//         context_draw_text(button_window->context,
//                           button_window->title,
//                           (button_window->width / 2) - (title_len / 2),
//                           (button_window->height / 2) - 6,
//                           WIN_TEXT_COLOR);
//     }
// }
//
// void client_button_mousedown_handler(const window_t *button_window, int16_t x, int16_t y)
// {
//     button_t *button = (button_t *)button_window;
//
//     (void)x;
//     (void)y;
//
//     window_invalidate((window_t *)button, 0, 0, button->window.height - 1, button->window.width - 1);
//
//     if (button->onmousedown) {
//         button->onmousedown(button, x, y);
//     }
// }