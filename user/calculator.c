#include <wm/imui.h>
#include <wm/window.h>
#include <wm/wm_protocol.h>
#include <wm/wmclient.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>

#define CALC_WIDTH 145
#define CALC_HEIGHT 170

#define DISPLAY_X 5
#define DISPLAY_Y 3
#define DISPLAY_W 135
#define DISPLAY_H 20

#define BUTTON_COLS 4
#define BUTTON_ROWS 4
#define BUTTON_W 30
#define BUTTON_H 30
#define BUTTON_GAP 5
#define BUTTON_START_X 5
#define BUTTON_START_Y 28

#define DISPLAY_CAPACITY 24

static constexpr char calc_buttons[BUTTON_ROWS][BUTTON_COLS] = {
    {'7', '8', '9', '+'},
    {'4', '5', '6', '-'},
    {'1', '2', '3', '*'},
    {'C', '0', '=', '/'},
};


static int text_pixel_width(const char *text)
{
    if (!text)
        return 0;
    return (int)strlen(text) * VESA_CHAR_WIDTH;
}

static void draw_display(imui_context_t *ui, const char *display)
{
    imui_fill_rect(ui, DISPLAY_X, DISPLAY_Y, DISPLAY_W, DISPLAY_H, 0xFFF1F3F5u);
    imui_fill_rect(ui, DISPLAY_X, DISPLAY_Y, DISPLAY_W, 1, 0xFF111111u);
    imui_fill_rect(ui, DISPLAY_X, (int16_t)(DISPLAY_Y + DISPLAY_H - 1), DISPLAY_W, 1, 0xFF111111u);
    imui_fill_rect(ui, DISPLAY_X, DISPLAY_Y, 1, DISPLAY_H, 0xFF111111u);
    imui_fill_rect(ui, (int16_t)(DISPLAY_X + DISPLAY_W - 1), DISPLAY_Y, 1, DISPLAY_H, 0xFF111111u);

    constexpr int max_display_chars = (DISPLAY_W - 6) / VESA_CHAR_WIDTH;
    const size_t display_len        = strlen(display);
    const char *shown               = display;
    if (display_len > (size_t)max_display_chars)
        shown = display + (display_len - (size_t)max_display_chars);

    int text_x = DISPLAY_X + DISPLAY_W - 3 - text_pixel_width(shown);
    if (text_x < DISPLAY_X + 3)
        text_x = DISPLAY_X + 3;

    constexpr int text_y = DISPLAY_Y + (DISPLAY_H - VESA_CHAR_HEIGHT) / 2;
    imui_text(ui, (int16_t)text_x, (int16_t)text_y, shown, 0xFF101010u);
}

static void display_reset(char *display)
{
    display[0] = '0';
    display[1] = '\0';
}

static void display_append_digit(char *display, const char digit)
{
    const size_t len = strlen(display);

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

static void process_button_press(char *display, const char label)
{
    if (label >= '0' && label <= '9') {
        display_append_digit(display, label);
        return;
    }

    if (label == 'C')
        display_reset(display);
}

static void render_calculator(imui_context_t *ui, char *display)
{
    if (!ui || !ui->window)
        return;

    imui_fill_rect(ui, 0, 0, ui->window->width, ui->window->height, ui->style.background_color);

    imui_set_cursor(ui, BUTTON_START_X, BUTTON_START_Y);
    for (int row = 0; row < BUTTON_ROWS; row++) {
        for (int col = 0; col < BUTTON_COLS; col++) {
            const char label = calc_buttons[row][col];
            char text[2]     = {label, '\0'};
            if (imui_button(ui, text, BUTTON_W, BUTTON_H))
                process_button_press(display, label);
            if (col + 1 < BUTTON_COLS)
                imui_same_line(ui, BUTTON_GAP);
        }
    }

    draw_display(ui, display);
}

int main(void)
{
    signal(SIGINT, SIG_IGN);
    wm_window_t *win = wm_create_window(115,
                                        60,
                                        CALC_WIDTH,
                                        CALC_HEIGHT,
                                        WIN_CLOSEABLE | WIN_MINIMIZABLE,
                                        0,
                                        "Calculator");
    if (!win)
        exit(1);

    imui_context_t ui = {};
    if (!imui_init(&ui, win)) {
        wm_destroy_window(win);
        exit(2);
    }
    ui.style = (imui_style_t){
        .background_color = 0xff62A0EA,
        .button_color = 0xffFF7800,
        .button_hot_color = 0xffC64600,
        .button_active_color = 0xff613583,
        .button_border_color = 0xffE5A50A,
        .text_color = 0xff000000,
        .item_spacing_x = 4,
        .item_spacing_y = 4
    };

    char display[DISPLAY_CAPACITY] = {0};
    display_reset(display);

    imui_begin_frame(&ui, nullptr);
    render_calculator(&ui, display);
    imui_end_frame(&ui);

    uint8_t event_buf[64];
    uint8_t event_type = 0;

    while (wm_next_event(event_buf, &event_type) == 0) {
        if (event_type == WM_EVENT_WINDOW_CLOSED) {
            auto ev = (const wm_event_window_closed_t *)event_buf;
            if (ev->window_id == win->window_id)
                break;
            continue;
        }

        const wm_event_mouse_t *mouse_event = nullptr;
        if (event_type == WM_EVENT_MOUSE) {
            auto ev = (const wm_event_mouse_t *)event_buf;
            if (ev->window_id == win->window_id)
                mouse_event = ev;
        }

        imui_begin_frame(&ui, mouse_event);
        render_calculator(&ui, display);
        imui_end_frame(&ui);
    }

    wm_shutdown_events();
    imui_deinit(&ui);
    wm_destroy_window(win);
    return 0;
}