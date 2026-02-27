#pragma once

#include <stdint.h>

#define MOUSE_PORT 0x60
#define MOUSE_STATUS 0x64
#define MOUSE_WRITE 0xD4
#define MOUSE_A_BIT 0x02
#define MOUSE_B_BIT 0x01
#define MOUSE_F_BIT 0x20
#define MOUSE_V_BIT 0x08

// Commands
#define MOUSE_SCALING_1_TO_1 0xE6
#define MOUSE_SCALING_2_TO_1 0xE7
#define MOUSE_SET_RESOLUTION 0xE8
#define MOUSE_RESOLUTION_1_MM 0x00
#define MOUSE_RESOLUTION_2_MM 0x01
#define MOUSE_RESOLUTION_4_MM 0x02
#define MOUSE_RESOLUTION_8_MM 0x03
#define MOUSE_STATUS_REQUEST 0xE9
#define MOUSE_SET_STREAM_MODE 0xEA
#define MOUSE_ENABLE_DATA_REPORTING 0xF4
#define MOUSE_READ_DATA 0xEB
#define MOUSE_SET_SAMPLE_RATE 0xF3
#define MOUSE_ENABLE_AUXILIARY_DEVICE 0xA8
#define MOUSE_SET_DEFAULTS 0xF6
#define MOUSE_RESET 0xFF

// Replies
#define MOUSE_REPLY_ACK 0xFA
#define MOUSE_REPLY_NACK 0xFE

// Flags
#define MOUSE_X_OVERFLOW (1 << 6)
#define MOUSE_Y_OVERFLOW (1 << 7)

// Mouse clicks
#define MOUSE_LEFT (1 << 0)
#define MOUSE_RIGHT (1 << 1)
#define MOUSE_MIDDLE (1 << 2)

typedef struct mouse
{
    int x;
    int y;
    uint8_t flags;
    uint8_t prev_flags;
} mouse_t;

typedef void (*mouse_callback)(mouse_t);

struct ps2_mouse_packet
{
    uint8_t flags;
    int16_t x;
    int16_t y;
};

struct ps2_mouse
{
    uint8_t cycle;
    int16_t x;
    int16_t y;
    uint8_t flags;
    uint8_t prev_flags;
    int16_t prev_x;
    int16_t prev_y;
    struct ps2_mouse_packet packet;
    bool dragging;
    uint8_t received;
    uint8_t initialized;
};

void mouse_init(void);
void mouse_get_position(mouse_t *mouse);
void mouse_set_position(int16_t x, int16_t y);
void mouse_flush_pending_events(void);
/**
 * @brief Inject a relative mouse movement event into the input subsystem.
 *
 * Applies the delta to the current cursor position, clamps to the framebuffer
 * bounds, and pushes a packet into the mouse event buffer.
 *
 * @param dx Horizontal displacement in pixels.
 * @param dy Vertical displacement in pixels.
 * @param buttons Button state bitmask (MOUSE_LEFT, MOUSE_RIGHT, MOUSE_MIDDLE).
 */
void mouse_inject_event(int16_t dx, int16_t dy, uint8_t buttons);

/**
 * @brief Set the mouse cursor to an absolute screen position.
 *
 * Overwrites the current position, clamps to the framebuffer bounds, and
 * pushes a packet into the mouse event buffer. Used by USB tablet devices
 * that report absolute coordinates.
 *
 * @param x Absolute horizontal position in pixels.
 * @param y Absolute vertical position in pixels.
 * @param buttons Button state bitmask (MOUSE_LEFT, MOUSE_RIGHT, MOUSE_MIDDLE).
 */
void mouse_set_absolute(int16_t x, int16_t y, uint8_t buttons);

