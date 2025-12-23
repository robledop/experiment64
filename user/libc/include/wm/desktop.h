#pragma once

#include <wm/video_context.h>
#include <wm/window.h>
#include <stdint.h>

#define MOUSE_WIDTH 11
#define MOUSE_HEIGHT 18
#define MOUSE_BUFSZ (MOUSE_WIDTH * MOUSE_HEIGHT)

#define DESKTOP_BACKGROUND_COLOR 0x113399

typedef struct desktop
{
    window_t window;
    int16_t mouse_x;
    int16_t mouse_y;
    uint32_t *wallpaper;
    uint16_t wallpaper_width;
    uint16_t wallpaper_height;
} desktop_t;

desktop_t *desktop_new(video_context_t *context, uint32_t *wallpaper, uint16_t wallpaper_width, uint16_t wallpaper_height);
void desktop_paint_handler(window_t *desktop_window);
void desktop_process_mouse(desktop_t *desktop, uint16_t mouse_x, uint16_t mouse_y, uint16_t mouse_buttons);

