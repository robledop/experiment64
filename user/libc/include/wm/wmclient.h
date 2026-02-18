#pragma once

#include <wm/wm_protocol.h>
#include <stdint.h>

typedef struct
{
    uint32_t window_id;
    uint16_t width;
    uint16_t height;
    uint32_t *buffer;
    int shm_fd;
} wm_window_t;

wm_window_t *wm_create_window(int16_t x, int16_t y, uint16_t width, uint16_t height, const char *title);
void wm_invalidate(const wm_window_t *win, int16_t x, int16_t y, uint16_t w, uint16_t h);
void wm_invalidate_all(const wm_window_t *win);
void wm_destroy_window(wm_window_t *win);
int wm_next_event(void *event_buf, uint8_t *out_type);
