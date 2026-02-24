#include <wm/wmclient.h>
#include <wm/wm_protocol.h>
#include <wm/video_context.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <util.h>
#include <wm/window.h>
#include "stddef.h"

#define CALC_WIDTH 145
#define CALC_HEIGHT 170

#define DISPLAY_X 5
#define DISPLAY_Y 3
#define DISPLAY_W 135
#define DISPLAY_H 20

#define BUTTON_W 30
#define BUTTON_H 30
#define BUTTON_GAP 5
#define BUTTON_START_X 5
#define BUTTON_START_Y 28

#define EVENT_DECOR_X 2
#define EVENT_DECOR_Y 25

#define DISPLAY_CAPACITY 24

typedef struct
{
    char label;
    int x;
    int y;
    int w;
    int h;
} calc_button_t;

static const calc_button_t calc_buttons[] = {
    {'7', BUTTON_START_X + 0 * (BUTTON_W + BUTTON_GAP), BUTTON_START_Y + 0 * (BUTTON_H + BUTTON_GAP), BUTTON_W,
     BUTTON_H},
    {'8', BUTTON_START_X + 1 * (BUTTON_W + BUTTON_GAP), BUTTON_START_Y + 0 * (BUTTON_H + BUTTON_GAP), BUTTON_W,
     BUTTON_H},
    {'9', BUTTON_START_X + 2 * (BUTTON_W + BUTTON_GAP), BUTTON_START_Y + 0 * (BUTTON_H + BUTTON_GAP), BUTTON_W,
     BUTTON_H},
    {'+', BUTTON_START_X + 3 * (BUTTON_W + BUTTON_GAP), BUTTON_START_Y + 0 * (BUTTON_H + BUTTON_GAP), BUTTON_W,
     BUTTON_H},
    {'4', BUTTON_START_X + 0 * (BUTTON_W + BUTTON_GAP), BUTTON_START_Y + 1 * (BUTTON_H + BUTTON_GAP), BUTTON_W,
     BUTTON_H},
    {'5', BUTTON_START_X + 1 * (BUTTON_W + BUTTON_GAP), BUTTON_START_Y + 1 * (BUTTON_H + BUTTON_GAP), BUTTON_W,
     BUTTON_H},
    {'6', BUTTON_START_X + 2 * (BUTTON_W + BUTTON_GAP), BUTTON_START_Y + 1 * (BUTTON_H + BUTTON_GAP), BUTTON_W,
     BUTTON_H},
    {'-', BUTTON_START_X + 3 * (BUTTON_W + BUTTON_GAP), BUTTON_START_Y + 1 * (BUTTON_H + BUTTON_GAP), BUTTON_W,
     BUTTON_H},
    {'1', BUTTON_START_X + 0 * (BUTTON_W + BUTTON_GAP), BUTTON_START_Y + 2 * (BUTTON_H + BUTTON_GAP), BUTTON_W,
     BUTTON_H},
    {'2', BUTTON_START_X + 1 * (BUTTON_W + BUTTON_GAP), BUTTON_START_Y + 2 * (BUTTON_H + BUTTON_GAP), BUTTON_W,
     BUTTON_H},
    {'3', BUTTON_START_X + 2 * (BUTTON_W + BUTTON_GAP), BUTTON_START_Y + 2 * (BUTTON_H + BUTTON_GAP), BUTTON_W,
     BUTTON_H},
    {'*', BUTTON_START_X + 3 * (BUTTON_W + BUTTON_GAP), BUTTON_START_Y + 2 * (BUTTON_H + BUTTON_GAP), BUTTON_W,
     BUTTON_H},
    {'C', BUTTON_START_X + 0 * (BUTTON_W + BUTTON_GAP), BUTTON_START_Y + 3 * (BUTTON_H + BUTTON_GAP), BUTTON_W,
     BUTTON_H},
    {'0', BUTTON_START_X + 1 * (BUTTON_W + BUTTON_GAP), BUTTON_START_Y + 3 * (BUTTON_H + BUTTON_GAP), BUTTON_W,
     BUTTON_H},
    {'=', BUTTON_START_X + 2 * (BUTTON_W + BUTTON_GAP), BUTTON_START_Y + 3 * (BUTTON_H + BUTTON_GAP), BUTTON_W,
     BUTTON_H},
    {'/', BUTTON_START_X + 3 * (BUTTON_W + BUTTON_GAP), BUTTON_START_Y + 3 * (BUTTON_H + BUTTON_GAP), BUTTON_W,
     BUTTON_H},
};

static video_context_t *video_context = nullptr;

static int text_pixel_width(const char *text, int scale)
{
    size_t len = strlen(text);
    if (len == 0)
        return 0;
    return (int)(len * (size_t)(7 * scale + scale) - (size_t)scale);
}

static void render_calculator(const wm_window_t *win, const char *display)
{
    context_fill_rect(video_context, 0, 0, win->width, win->height, 0xFF1C1F24);

    context_fill_rect(video_context, DISPLAY_X, DISPLAY_Y, DISPLAY_W, DISPLAY_H, 0xFFF1F3F5);
    context_fill_rect(video_context, DISPLAY_X, DISPLAY_Y, DISPLAY_W, 1, 0xFF111111);
    context_fill_rect(video_context, DISPLAY_X, DISPLAY_Y + DISPLAY_H - 1, DISPLAY_W, 1, 0xFF111111);
    context_fill_rect(video_context, DISPLAY_X, DISPLAY_Y, 1, DISPLAY_H, 0xFF111111);
    context_fill_rect(video_context, DISPLAY_X + DISPLAY_W - 1, DISPLAY_Y, 1, DISPLAY_H, 0xFF111111);

    int max_display_chars = (DISPLAY_W - 6) / 6;
    size_t display_len    = strlen(display);
    const char *shown     = display;
    if (display_len > (size_t)max_display_chars)
        shown = display + (display_len - (size_t)max_display_chars);

    int text_w = text_pixel_width(shown, 1);
    int text_x = DISPLAY_X + DISPLAY_W - 3 - text_w;
    if (text_x < DISPLAY_X + 3)
        text_x = DISPLAY_X + 3;
    // draw_text(win->buffer, win->width, text_x, DISPLAY_Y + 6, shown, 1, 0xFF101010);
    context_draw_text(video_context, shown, text_x, DISPLAY_Y + 6, 0xFF101010);

    for (size_t i = 0; i < ARRAY_SIZE(calc_buttons); i++) {
        const calc_button_t *btn = &calc_buttons[i];

        context_fill_rect(video_context, btn->x, btn->y, btn->w, btn->h, 0xFFE7EAEE);
        context_fill_rect(video_context, btn->x, btn->y, btn->w, 1, 0xFF2A2F36);
        context_fill_rect(video_context, btn->x, btn->y + btn->h - 1, btn->w, 1, 0xFF2A2F36);
        context_fill_rect(video_context, btn->x, btn->y, 1, btn->h, 0xFF2A2F36);
        context_fill_rect(video_context, btn->x + btn->w - 1, btn->y, 1, btn->h, 0xFF2A2F36);

        // fill_rect(win->buffer, win->width, btn->x, btn->y, btn->w, btn->h, 0xFFE7EAEE);
        // fill_rect(win->buffer, win->width, btn->x, btn->y, btn->w, 1, 0xFF2A2F36);
        // fill_rect(win->buffer, win->width, btn->x, btn->y + btn->h - 1, btn->w, 1, 0xFF2A2F36);
        // fill_rect(win->buffer, win->width, btn->x, btn->y, 1, btn->h, 0xFF2A2F36);
        // fill_rect(win->buffer, win->width, btn->x + btn->w - 1, btn->y, 1, btn->h, 0xFF2A2F36);

        char label_text[2]   = {btn->label, '\0'};
        int label_w          = text_pixel_width(label_text, 1);
        int label_x          = btn->x + (btn->w - label_w) / 2;
        int label_y          = btn->y + (btn->h - VESA_CHAR_HEIGHT) / 2;
        uint32_t label_color = (btn->label == 'C') ? 0xFFAA1E1E : 0xFF111111;
        // draw_text(win->buffer, win->width, label_x, label_y, label_text, 2, label_color);
        context_draw_text(video_context, label_text, label_x, label_y, label_color);
    }
}

static int button_index_at(int x, int y)
{
    for (size_t i = 0; i < sizeof(calc_buttons) / sizeof(calc_buttons[0]); i++) {
        const calc_button_t *btn = &calc_buttons[i];
        if (x >= btn->x && x < (btn->x + btn->w) && y >= btn->y && y < (btn->y + btn->h))
            return (int)i;
    }
    return -1;
}

static int normalize_mouse_coords(int raw_x, int raw_y, int *x_out, int *y_out)
{
    int adjusted_x = raw_x - EVENT_DECOR_X;
    int adjusted_y = raw_y - EVENT_DECOR_Y;

    if (adjusted_x >= 0 && adjusted_x < CALC_WIDTH && adjusted_y >= 0 && adjusted_y < CALC_HEIGHT) {
        *x_out = adjusted_x;
        *y_out = adjusted_y;
        return 0;
    }

    if (raw_x >= 0 && raw_x < CALC_WIDTH && raw_y >= 0 && raw_y < CALC_HEIGHT) {
        *x_out = raw_x;
        *y_out = raw_y;
        return 0;
    }

    return -1;
}

static void display_reset(char *display)
{
    display[0] = '0';
    display[1] = '\0';
}

static void display_append_digit(char *display, char digit)
{
    size_t len = strlen(display);

    if (digit == '0' && len == 1 && display[0] == '0')
        return;

    if (len == 1 && display[0] == '0' && digit != '0') {
        display[0] = digit;
        return;
    }

    if (len + 1 >= DISPLAY_CAPACITY)
        return;

    display[len]     = digit;
    display[len + 1] = '\0';
}

static void process_button_press(char *display, char label)
{
    if (label >= '0' && label <= '9') {
        display_append_digit(display, label);
        return;
    }

    if (label == 'C') {
        display_reset(display);
    }
}

int main(void)
{
    wm_window_t *win = wm_create_window(115, 60, CALC_WIDTH, CALC_HEIGHT + 50 + WIN_TITLE_HEIGHT + WIN_BORDER_WIDTH, WIN_CLOSEABLE,0, "Calculator");
    if (!win) {
        exit(1);
    }

    video_context = context_new(win->buffer, win->width, win->height, win->width * 4);
    if (!video_context) {
        wm_destroy_window(win);
        exit(2);
    }

    char display[DISPLAY_CAPACITY];
    display_reset(display);
    render_calculator(win, display);

    wm_window_t *button = wm_create_window(0, CALC_HEIGHT + WIN_TITLE_HEIGHT + WIN_BORDER_WIDTH, CALC_WIDTH, 50, WIN_NODECORATION,win->window_id, "Button");
    // wm_window_insert_child(win->window_id, button->window_id);

    wm_invalidate_all(win);
    // wm_invalidate_all(button);

    uint8_t event_buf[64];
    uint8_t event_type = 0;

    while (wm_next_event(event_buf, &event_type) == 0) {
        if (event_type == WM_EVENT_MOUSE) {
            auto ev = (wm_event_mouse_t *)event_buf;
            int x   = 0;
            int y   = 0;
            if (normalize_mouse_coords(ev->x, ev->y, &x, &y) != 0)
                continue;

            int button_index = button_index_at(x, y);
            if (button_index < 0)
                continue;

            process_button_press(display, calc_buttons[button_index].label);
            render_calculator(win, display);
            // wm_invalidate_all(win);
            // wm_invalidate_all(win);
            wm_invalidate_region(win, DISPLAY_X, DISPLAY_Y, DISPLAY_W, DISPLAY_H);
            wm_invalidate_region(win, DISPLAY_X, DISPLAY_Y, DISPLAY_W, DISPLAY_H);
            wm_invalidate_all(button);
        } else if (event_type == WM_EVENT_WINDOW_CLOSED) {

            auto ev = (const wm_event_window_closed_t *)event_buf;
            if (ev->window_id == win->window_id) {
                break;
            }
        }
    }

    wm_shutdown_events();
    wm_destroy_window(win);
    return 0;
}