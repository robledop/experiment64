#include <wm/wmclient.h>
#include <wm/wm_protocol.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>

#include "wm/button.h"

#define EVENT_DECOR_X 2
#define EVENT_DECOR_Y 25
#define DEMO_WIDTH 200
#define DEMO_HEIGHT 150

static void fill_rect(uint32_t *buf, uint16_t stride, int x, int y,
                      int w, int h, uint32_t color)
{
    for (int row = y; row < y + h; row++)
        for (int col = x; col < x + w; col++)
            buf[row * stride + col] = color;
}


int normalize_mouse_coords(wm_window_t *win, int raw_x, int raw_y, int *x_out, int *y_out)
{
    int adjusted_x = raw_x - EVENT_DECOR_X;
    int adjusted_y = raw_y - EVENT_DECOR_Y;

    if (adjusted_x >= 0 && adjusted_x < win->width && adjusted_y >= 0 && adjusted_y < win->height) {
        *x_out = adjusted_x;
        *y_out = adjusted_y;
        return 0;
    }

    return -1;
}

static void render_screen(const wm_window_t *win)
{
    memset(win->buffer, 0xFF, (size_t)win->width * win->height * 4);
    fill_rect(win->buffer, win->width, 10, 10, 80, 60, 0xFFFF4444);
    fill_rect(win->buffer, win->width, 110, 10, 80, 60, 0xFF44FF44);
    fill_rect(win->buffer, win->width, 10, 80, 80, 60, 0xFF4444FF);
    fill_rect(win->buffer, win->width, 110, 80, 80, 60, 0xFFFFFF44);
}


void crash_button_handler(const struct button *, int x, int y)
{
    *((int *)0) = 0;
}

int main(void)
{
    wm_window_t *win = wm_create_window(50, 60, DEMO_WIDTH, DEMO_HEIGHT, WIN_CLOSEABLE, "Demo Client");
    if (!win)
        exit(1);

    // button_t *crash_button    = button_new(10, 10, 100, 30);
    // crash_button->onmousedown = crash_button_handler;
    // window_set_title((window_t *)crash_button, "Crash");
    // window_insert_child((window_t *)get_window(win->window_id), (window_t *)crash_button);

    render_screen(win);

    wm_invalidate_all(win);

    uint8_t event_buf[64];
    uint8_t event_type;
    while (wm_next_event(event_buf, &event_type) == 0) {
        if (event_type == WM_EVENT_MOUSE) {
            auto ev = (wm_event_mouse_t *)event_buf;
            int x   = 0;
            int y   = 0;
            if (normalize_mouse_coords(win, ev->x, ev->y, &x, &y) != 0)
                continue;
            render_screen(win);
            fill_rect(win->buffer, win->width, x, y, 10, 10, 0xFFFF0000);
            wm_invalidate_region(win, (int16_t)x, (int16_t)y, 10, 10);
        } else if (event_type == WM_EVENT_KEY) {
            wm_event_key_t *ev = (wm_event_key_t *)event_buf;
            if (!ev->pressed)
                continue;

            uint32_t color = 0xFF000000u | ((((uint32_t)ev->keycode * 2654435761u) >> 8) & 0x00FFFFFFu);
            int cx         = ((int)ev->keycode * 37) % (win->width - 10);
            int cy         = ((int)ev->keycode * 53) % (win->height - 10);

            render_screen(win);
            fill_rect(win->buffer, win->width, cx, cy, 10, 10, color);
            wm_invalidate_region(win, (int16_t)cx, (int16_t)cy, 10, 10);
        } else if (event_type == WM_EVENT_WINDOW_CLOSED) {

            auto ev = (const wm_event_window_closed_t *)event_buf;
            if (ev->window_id == win->window_id) {
                break;
            }
        }
    }

    wm_destroy_window(win);
    return 0;
}