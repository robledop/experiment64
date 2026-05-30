/**
 * @file wm_protocol.h
 * @brief Wire format shared by the WM client library and the WM server.
 *
 * This header is the keystone contract between the two halves of the window
 * system: the client side (user/libc/src/wmclient.c) and the server side
 * (user/wm/wm_client.c). Every struct here is __attribute__((packed)) so the
 * same bytes mean the same thing on both ends.
 *
 * Two-pipe IPC model. A launched client inherits two inherited file
 * descriptors:
 *   - WM_CMD_FD (4): client -> server. Carries enum wm_msg_type requests
 *     (CREATE_WINDOW, INSERT_CHILD, INVALIDATE, DESTROY_WINDOW). The server
 *     reads this as cmd_fd in wm_client.c's command loop.
 *   - WM_EVT_FD (3): server -> client. Carries enum wm_event_type messages
 *     (WINDOW_CREATED, MOUSE, KEY, RESIZED, CLOSED, INVALIDATED). The client's
 *     reader thread (wm_event_reader_thread) reads this; the server writes it
 *     as cw->evt_fd.
 *
 * WM_MSG_INVALIDATE is a SYNCHRONOUS present: the client blocks in
 * wm_invalidate_region() until the matching WM_EVENT_INVALIDATED acks the
 * composite. Round trip:
 *
 *   client                              server
 *     |  WM_MSG_INVALIDATE (CMD_FD) ------> |
 *     |  (blocks on present cond var)       | composite back buffer to screen
 *     | <----- WM_EVENT_INVALIDATED (EVT_FD)|
 *     |  (reader thread wakes the waiter)   |
 *     v                                     v
 *   returns
 */
#pragma once

#include <stdint.h>

#define WM_EVT_FD 3
#define WM_CMD_FD 4

#define WM_SHM_NAME_MAX 64
#define WM_TITLE_MAX 64
#define WM_BUFFER_COUNT 2

enum wm_msg_type {
    WM_MSG_CREATE_WINDOW       = 1,
    WM_MSG_WINDOW_INSERT_CHILD = 2,
    WM_MSG_DESTROY_WINDOW      = 3,
    WM_MSG_INVALIDATE          = 4,
};

enum wm_event_type {
    WM_EVENT_WINDOW_CREATED        = 1,
    WM_EVENT_WINDOW_CHILD_INSERTED = 2,
    WM_EVENT_MOUSE                 = 3,
    WM_EVENT_KEY                   = 4,
    WM_EVENT_WINDOW_RESIZED        = 5,
    WM_EVENT_WINDOW_CLOSED         = 6,
    WM_EVENT_INVALIDATED           = 7,
};

typedef struct __attribute__((packed)) {
    uint8_t type;
    uint16_t width;
    uint16_t height;
    int16_t x;
    int16_t y;
    uint16_t flags;
    uint16_t parent_id;
    char title[WM_TITLE_MAX];
} wm_msg_create_window_t;

typedef struct __attribute__((packed)) {
    uint8_t type;
    uint16_t parent_id;
    uint16_t child_id;
} wm_msg_window_insert_child_t;

typedef struct __attribute__((packed)) {
    uint8_t type;
    uint32_t window_id;
} wm_msg_destroy_window_t;

typedef struct __attribute__((packed)) {
    uint8_t type;
    uint32_t window_id;
    uint8_t buffer_index;
    int16_t x;
    int16_t y;
    uint16_t width;
    uint16_t height;
} wm_msg_invalidate_t;

typedef struct __attribute__((packed)) {
    uint8_t type;
    uint32_t window_id;
    uint16_t width;
    uint16_t height;
    uint8_t front_buffer;
    char shm_names[WM_BUFFER_COUNT][WM_SHM_NAME_MAX];
} wm_event_window_created_t;

typedef struct __attribute__((packed)) {
    uint8_t type;
    uint32_t window_id;
    int16_t x;
    int16_t y;
    uint8_t buttons;
} wm_event_mouse_t;

typedef struct __attribute__((packed)) {
    uint8_t type;
    uint32_t window_id;
    uint8_t keycode;
    uint8_t pressed;
} wm_event_key_t;

typedef struct __attribute__((packed)) {
    uint8_t type;
    uint32_t window_id;
    uint16_t width;
    uint16_t height;
} wm_event_window_resized_t;

typedef struct __attribute__((packed)) {
    uint8_t type;
    uint32_t window_id;
    uint16_t width;
    uint16_t height;
    uint8_t front_buffer;
    char shm_names[WM_BUFFER_COUNT][WM_SHM_NAME_MAX];
} wm_event_window_resized_msg_t;

typedef struct __attribute__((packed)) {
    uint8_t type;
    uint32_t window_id;
} wm_event_window_closed_t;

typedef struct __attribute__((packed)) {
    uint8_t type;
    uint32_t window_id;
    uint8_t front_buffer;
} wm_event_invalidated_t;
