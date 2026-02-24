#include <wm/textbox.h>
#include <wm/window.h>
#include <wm/desktop.h>
#include <stdint.h>

#include "stdlib.h"
#include "string.h"

/**
 * @brief Create a textbox widget at the specified location.
 *
 * @param x Left position in pixels.
 * @param y Top position in pixels.
 * @param width Textbox width in pixels.
 * @param height Textbox height in pixels.
 * @return Pointer to the new textbox or nullptr on failure.
 */
textbox_t *textbox_new(int16_t x, int16_t y, int width, int height)
{
    auto text_box = (textbox_t *)malloc(sizeof(textbox_t));
    if (!text_box) {
        return text_box;
    }

    if (!window_init((window_t *)text_box, x, y, width, height, WIN_NODECORATION, nullptr)) {
        free(text_box);
        return nullptr;
    }

    // Override default window draw callback
    text_box->window.paint_function = textbox_paint;

    return text_box;
}

/**
 * @brief Paint callback used to render the textbox background and text.
 *
 * @param text_box_window Window representing the textbox.
 */
void textbox_paint(const window_t *text_box_window)
{
    // White background
    context_fill_rect(
        text_box_window->context,
        1,
        1,
        text_box_window->width - 2,
        text_box_window->height - 2,
        0xFFFFFFFF);

    // Simple black border
    context_draw_rect(text_box_window->context, 0, 0, text_box_window->width, text_box_window->height, 0xFF000000);

    int title_len = (int)strlen(text_box_window->title);

    title_len *= VESA_CHAR_WIDTH;

    // Draw the title centered within the button
    if (text_box_window->title) {
        context_draw_text(text_box_window->context,
                          text_box_window->title,
                          text_box_window->width - title_len - 6,
                          (text_box_window->height / 2) - 6,
                          WIN_TEXT_COLOR);
    }
}