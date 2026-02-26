#include <wm/wmclient.h>
#include <signal.h>
#include <wm/wm_protocol.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>

#include "wm/button.h"
#include "wm/imui.h"

#define EVENT_DECOR_X 2
#define EVENT_DECOR_Y 25
#define DEMO_WIDTH 200
#define DEMO_HEIGHT 150

void crash_button_handler()
{
    *((int *)0) = 0;
}

static void render_screen(imui_context_t *ui)
{
    imui_fill_rect(ui, 10, 10, 80, 60, 0xFFFF4444);
    imui_fill_rect(ui, 110, 10, 80, 60, 0xFF44FF44);
    imui_fill_rect(ui, 10, 80, 80, 60, 0xFF4444FF);
    imui_fill_rect(ui,  110, 80, 80, 60, 0xFFFFFF44);

    if (imui_button(ui, "Crash", 70, 30))
        crash_button_handler();
}


int main(void)
{
    signal(SIGINT, SIG_IGN);
    wm_window_t *win = wm_create_window(50, 60, DEMO_WIDTH, DEMO_HEIGHT, WIN_CLOSEABLE, 0, "Demo Client");
    if (!win)
        exit(1);

    imui_context_t ui = {};
    if (!imui_init(&ui, win)) {
        wm_destroy_window(win);
        exit(2);
    }

    imui_begin_frame(&ui, nullptr);
    render_screen(&ui);
    imui_end_frame(&ui);

    uint8_t event_buf[64];
    uint8_t event_type;
    while (wm_next_event(event_buf, &event_type) == 0) {
        if (event_type == WM_EVENT_MOUSE) {
            auto ev = (wm_event_mouse_t *)event_buf;
            imui_begin_frame(&ui, ev);
            render_screen(&ui);
            imui_end_frame(&ui);
        } else if (event_type == WM_EVENT_KEY) {
            auto ev = (wm_event_key_t *)event_buf;

            imui_begin_frame(&ui, nullptr);
            render_screen(&ui);
            imui_end_frame(&ui);
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