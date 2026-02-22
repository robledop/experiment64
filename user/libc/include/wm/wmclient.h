#pragma once

#include <wm/wm_protocol.h>
#include <stdint.h>
#include <wm/window.h>

typedef struct
{
    uint32_t window_id;
    uint16_t width;
    uint16_t height;
    uint8_t front_buffer;
    uint8_t back_buffer;
    uint32_t *buffers[2];
    uint32_t *buffer;
    int shm_fds[2];
    uint32_t presents_requested;
    uint32_t presents_completed;
} wm_window_t;

wm_window_t *wm_create_window(int16_t x, int16_t y, uint16_t width, uint16_t height, uint16_t flags, const char *title);
void wm_invalidate(wm_window_t *win);
void wm_invalidate_region(wm_window_t *win, int16_t x, int16_t y, uint16_t w, uint16_t h);
void wm_invalidate_all(wm_window_t *win);
void wm_destroy_window(wm_window_t *win);
int wm_next_event(void *event_buf, uint8_t *out_type);
void wm_shutdown_events(void);
