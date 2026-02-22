#pragma once

#include <wm/window.h>
#include <wm/wm_protocol.h>
#include <stdint.h>

#define WM_MAX_CLIENTS 8


typedef struct
{
    window_t window;
    uint32_t *shm_buffers[2];
    uint16_t content_width;
    uint16_t content_height;
    uint32_t window_id;
    uint8_t front_buffer;
    uint32_t shm_generation;
    int cmd_fd;
    int evt_fd;
    int client_pid;
    char shm_names[2][WM_SHM_NAME_MAX];
} client_window_t;

typedef struct
{
    client_window_t *clients[WM_MAX_CLIENTS];
    int count;
    uint32_t next_window_id;
    bool initialized;
} client_manager_t;

void client_manager_init(client_manager_t *mgr);

int client_launch(window_t *parent, const char *path, int16_t default_x, int16_t default_y);

void client_window_paint_handler(const window_t *window);

void client_window_mousedown_handler(const window_t *window, int16_t x, int16_t y);

void client_dispatch_key_event(const window_t *parent, uint8_t keycode, uint8_t pressed);

client_window_t *find_client_by_window_id(uint32_t window_id);

void wm_state_lock(void);

void wm_state_unlock(void);

void handle_destroy_window([[maybe_unused]] window_t *parent,
                           const wm_msg_destroy_window_t *msg);