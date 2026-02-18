#pragma once

#include <stdint.h>

#define WM_EVT_FD 3
#define WM_CMD_FD 4

#define WM_SHM_NAME_MAX 64
#define WM_TITLE_MAX 64

enum wm_msg_type
{
    WM_MSG_CREATE_WINDOW = 1,
    WM_MSG_DESTROY_WINDOW = 2,
    WM_MSG_INVALIDATE = 3,
};

enum wm_event_type
{
    WM_EVENT_WINDOW_CREATED = 1,
    WM_EVENT_MOUSE = 2,
    WM_EVENT_KEY = 3,
    WM_EVENT_WINDOW_RESIZED = 4,
    WM_EVENT_WINDOW_CLOSED = 5,
};

typedef struct __attribute__((packed))
{
    uint8_t type;
    uint16_t width;
    uint16_t height;
    int16_t x;
    int16_t y;
    char title[WM_TITLE_MAX];
} wm_msg_create_window_t;

typedef struct __attribute__((packed))
{
    uint8_t type;
    uint32_t window_id;
} wm_msg_destroy_window_t;

typedef struct __attribute__((packed))
{
    uint8_t type;
    uint32_t window_id;
    int16_t x;
    int16_t y;
    uint16_t width;
    uint16_t height;
} wm_msg_invalidate_t;

typedef struct __attribute__((packed))
{
    uint8_t type;
    uint32_t window_id;
    uint16_t width;
    uint16_t height;
    char shm_name[WM_SHM_NAME_MAX];
} wm_event_window_created_t;

typedef struct __attribute__((packed))
{
    uint8_t type;
    uint32_t window_id;
    int16_t x;
    int16_t y;
    uint8_t buttons;
} wm_event_mouse_t;

typedef struct __attribute__((packed))
{
    uint8_t type;
    uint32_t window_id;
    uint8_t keycode;
    uint8_t pressed;
} wm_event_key_t;

typedef struct __attribute__((packed))
{
    uint8_t type;
    uint32_t window_id;
    uint16_t width;
    uint16_t height;
} wm_event_window_resized_t;

typedef struct __attribute__((packed))
{
    uint8_t type;
    uint32_t window_id;
    uint16_t width;
    uint16_t height;
    char shm_name[WM_SHM_NAME_MAX];
} wm_event_window_resized_msg_t;

typedef struct __attribute__((packed))
{
    uint8_t type;
    uint32_t window_id;
} wm_event_window_closed_t;
