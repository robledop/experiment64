#include "taskbar.h"
#include <array.h>
#include <wm/button.h>
#include <wm/window.h>

static window_t *taskbar;
static taskbar_button_t *g_taskbar_buttons = nullptr;

static constexpr int16_t TASKBAR_BUTTON_WIDTH  = 100;
static constexpr int16_t TASKBAR_BUTTON_HEIGHT = 30;
static constexpr int16_t TASKBAR_BUTTON_GAP    = 5;

static int16_t taskbar_button_y()
{
    if (!taskbar || taskbar->height <= TASKBAR_BUTTON_HEIGHT)
        return 0;
    return (int16_t)((taskbar->height - TASKBAR_BUTTON_HEIGHT) / 2);
}

static int16_t taskbar_button_x_for_index(size_t index)
{
    return (int16_t)((TASKBAR_BUTTON_WIDTH + TASKBAR_BUTTON_GAP) * (int16_t)index + TASKBAR_BUTTON_GAP);
}


window_t *taskbar_get()
{
    return taskbar;
}

taskbar_button_t *taskbar_buttons_get()
{
    return g_taskbar_buttons;
}

void taskbar_init(int16_t x, int16_t y, uint16_t w, uint16_t h)
{
    taskbar = window_new(x, y, w, h, WIN_NODECORATION | WIN_BACKGROUND, nullptr);
    window_set_title(taskbar, "Taskbar");

    button_t *taskbar_button    = button_new(taskbar_button_x_for_index(0),
                                          taskbar_button_y(),
                                          TASKBAR_BUTTON_WIDTH,
                                          TASKBAR_BUTTON_HEIGHT);
    taskbar_button->onmousedown = nullptr;
    window_set_title((window_t *)taskbar_button, "START");
    window_insert_child((window_t *)taskbar, (window_t *)taskbar_button);
    window_paint((window_t *)taskbar, nullptr, 1);

    taskbar_button_t tb = {.button = taskbar_button, .window = nullptr};
    arr_push(g_taskbar_buttons, tb);
}

void taskbar_button_activate(int index)
{
    button_t *button = arr_get(g_taskbar_buttons, index).button;
    if (!button)
        return;
    button->color_toggle = 1;

    window_invalidate((window_t *)button, 0, 0, button->window.height - 1, button->window.width - 1);
    window_paint((window_t *)button, nullptr, 1);
    for (size_t i = 0; i < arr_len(g_taskbar_buttons); i++) {
        if ((int)i != index) {
            button_t *other_button = arr_get(g_taskbar_buttons, i).button;
            if (other_button) {
                if (other_button->color_toggle == 1) {
                    other_button->color_toggle = 0;
                    window_invalidate((window_t *)other_button,
                                      0,
                                      0,
                                      other_button->window.height - 1,
                                      other_button->window.width - 1);
                    window_paint((window_t *)other_button, nullptr, 1);
                }
            }
        }
    }
}

int taskbar_find_button(const button_t *button)
{
    for (size_t i = 0; i < arr_len(g_taskbar_buttons); i++) {
        if (arr_get(g_taskbar_buttons, i).button == button)
            return (int)i;
    }
    return -1;
}

int taskbar_find_window(const window_t *window)
{
    for (size_t i = 0; i < arr_len(g_taskbar_buttons); i++) {
        if (arr_get(g_taskbar_buttons, i).window == window)
            return (int)i;
    }
    return -1;
}

static void taskbar_button_mousedown_handler(button_t *button, [[maybe_unused]] int x, [[maybe_unused]] int y)
{
    int index = taskbar_find_button(button);
    if (index == -1)
        return;

    window_t *window = arr_get(g_taskbar_buttons, index).window;
    window_raise(window, true);
}

void taskbar_add_button(const char *title, window_t *window)
{
    size_t button_index = arr_len(g_taskbar_buttons);
    button_t *taskbar_button =
        button_new(taskbar_button_x_for_index(button_index), taskbar_button_y(), TASKBAR_BUTTON_WIDTH, TASKBAR_BUTTON_HEIGHT);
    taskbar_button->onmousedown = taskbar_button_mousedown_handler;
    window_set_title((window_t *)taskbar_button, title);
    window_insert_child(taskbar, (window_t *)taskbar_button);
    window_paint(taskbar, nullptr, 1);

    taskbar_button_t tb = {.button = taskbar_button, .window = window};
    arr_push(g_taskbar_buttons, tb);
}

void taskbar_remove_button(int idx)
{
    if (idx < 0)
        return;

    size_t button_index = (size_t)idx;
    if (button_index >= arr_len(g_taskbar_buttons))
        return;

    button_t *taskbar_button = arr_get(g_taskbar_buttons, button_index).button;
    if (taskbar_button && taskbar_button->window.parent)
        window_remove_child(taskbar_button->window.parent, (window_t *)taskbar_button);

    arr_remove_at(g_taskbar_buttons, button_index);

    size_t button_count = arr_len(g_taskbar_buttons);
    for (size_t i = button_index; i < button_count; ++i) {
        button_t *button = arr_get(g_taskbar_buttons, i).button;
        if (!button)
            continue;
        button->window.x = taskbar_button_x_for_index(i);
    }
}
