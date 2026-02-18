#include <wm/wmclient.h>
#include <wm/wm_protocol.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>

static void fill_rect(uint32_t *buf, uint16_t stride, int x, int y,
                      int w, int h, uint32_t color)
{
    for (int row = y; row < y + h; row++)
        for (int col = x; col < x + w; col++)
            buf[row * stride + col] = color;
}

int main(void)
{
    wm_window_t *win = wm_create_window(50, 60, 200, 150, "Demo Client");
    if (!win)
        exit(1);

    memset(win->buffer, 0xFF, (size_t)win->width * win->height * 4);

    fill_rect(win->buffer, win->width, 10, 10, 80, 60, 0xFFFF4444);
    fill_rect(win->buffer, win->width, 110, 10, 80, 60, 0xFF44FF44);
    fill_rect(win->buffer, win->width, 10, 80, 80, 60, 0xFF4444FF);
    fill_rect(win->buffer, win->width, 110, 80, 80, 60, 0xFFFFFF44);

    wm_invalidate_all(win);

    uint8_t event_buf[64];
    uint8_t event_type;
    while (wm_next_event(event_buf, &event_type) == 0)
    {
        if (event_type == WM_EVENT_MOUSE)
        {
            wm_event_mouse_t *ev = (wm_event_mouse_t *)event_buf;
            int cx = ev->x - 10;
            int cy = ev->y - 25;
            if (cx < 0) cx = 0;
            if (cy < 0) cy = 0;
            if (cx + 10 > win->width) cx = win->width - 10;
            if (cy + 10 > win->height) cy = win->height - 10;
            fill_rect(win->buffer, win->width, cx, cy, 10, 10, 0xFFFF0000);
            wm_invalidate(win, (int16_t)cx, (int16_t)cy, 10, 10);
        }
        else if (event_type == WM_EVENT_KEY)
        {
            wm_event_key_t *ev = (wm_event_key_t *)event_buf;
            if (!ev->pressed)
                continue;

            uint32_t color = 0xFF000000u | ((((uint32_t)ev->keycode * 2654435761u) >> 8) & 0x00FFFFFFu);
            int cx = ((int)ev->keycode * 37) % (win->width - 10);
            int cy = ((int)ev->keycode * 53) % (win->height - 10);
            fill_rect(win->buffer, win->width, cx, cy, 10, 10, color);
            wm_invalidate(win, (int16_t)cx, (int16_t)cy, 10, 10);
        }
        else if (event_type == WM_EVENT_WINDOW_CLOSED)
        {
            break;
        }
    }

    wm_destroy_window(win);
    return 0;
}
