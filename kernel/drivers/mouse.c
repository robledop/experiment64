#include <drivers/mouse.h>
#include <arch/x86_64/port_io.h>
#include <drivers/framebuffer.h>
#include <fs/devfs.h>
#include <fs/vfs.h>
#include <mem/heap.h>
#include <lib/string.h>
#include <task/process.h>
#include <task/spinlock.h>
#include <arch/x86_64/idt.h>
#include <arch/x86_64/apic.h>
#include "limine.h"

#define MOUSE_BUFFER_SIZE 32

static struct ps2_mouse mouse_device = {};
static struct ps2_mouse_packet mouse_buffer[MOUSE_BUFFER_SIZE];
static int mouse_buf_head = 0;
static int mouse_buf_tail = 0;
static spinlock_t mouse_lock;

static struct inode_operations mouse_dev_ops;

static void mouse_wait(unsigned char a_type)
{
    unsigned int timeout = 1000000;
    if (!a_type) {
        while (--timeout) {
            if ((inb(MOUSE_STATUS) & MOUSE_B_BIT) == 1) {
                return;
            }
        }
    } else {
        while (--timeout) {
            if (!((inb(MOUSE_STATUS) & MOUSE_A_BIT))) {
                return;
            }
        }
    }
}

void mouse_set_position(int16_t x, int16_t y)
{
    mouse_device.x = x;
    mouse_device.y = y;
}

static void mouse_device_write(uint8_t command)
{
    mouse_wait(1);
    outb(MOUSE_STATUS, MOUSE_WRITE);
    mouse_wait(1);
    outb(MOUSE_PORT, command);
}

static uint8_t mouse_device_read(void)
{
    mouse_wait(0);
    return inb(MOUSE_PORT);
}

static inline bool mouse_buffer_empty(void)
{
    return mouse_buf_head == mouse_buf_tail;
}

static void mouse_buffer_push(struct ps2_mouse_packet pkt)
{
    int next = (mouse_buf_head + 1) % MOUSE_BUFFER_SIZE;
    if (next == mouse_buf_tail) {
        mouse_buf_tail = (mouse_buf_tail + 1) % MOUSE_BUFFER_SIZE;
    }
    mouse_buffer[mouse_buf_head] = pkt;
    mouse_buf_head               = next;
}

static bool mouse_buffer_pop(struct ps2_mouse_packet *pkt)
{
    if (mouse_buffer_empty()) {
        return false;
    }
    *pkt           = mouse_buffer[mouse_buf_tail];
    mouse_buf_tail = (mouse_buf_tail + 1) % MOUSE_BUFFER_SIZE;
    return true;
}

static uint64_t mouse_dev_read(const vfs_inode_t *node, uint64_t offset, uint64_t size, uint8_t *buffer)
{
    (void)node;
    (void)offset;

    if (!buffer || size == 0)
        return 0;

    uint64_t rflags;
    SPIN_LOCK_INT_SAVE(mouse_lock, rflags);

    while (mouse_buffer_empty()) {
        SPIN_UNLOCK_INT_RESTORE(mouse_lock, rflags);
        thread_sleep(&mouse_device, nullptr);
        SPIN_LOCK_INT_SAVE(mouse_lock, rflags);
    }

    struct ps2_mouse_packet packet;
    mouse_buffer_pop(&packet);
    const uint64_t bytes = (size < sizeof(packet)) ? size : sizeof(packet);
    memcpy(buffer, &packet, (size_t)bytes);

    SPIN_UNLOCK_INT_RESTORE(mouse_lock, rflags);

    return bytes;
}

static void mouse_handler(struct interrupt_frame *frame)
{
    (void)frame;

    uint8_t status = inb(MOUSE_STATUS);
    while (status & MOUSE_B_BIT) {
        if ((status & MOUSE_F_BIT) == 0)
            break; // Keyboard data in the buffer — leave it for the keyboard ISR

        const int8_t byte = (int8_t)inb(MOUSE_PORT);

        switch (mouse_device.cycle) {
        case 0:
            mouse_device.packet.flags = (uint8_t)byte;
            if ((mouse_device.packet.flags & MOUSE_V_BIT) == 0) {
                mouse_device.cycle = 0;
                status             = inb(MOUSE_STATUS);
                continue;
            }
            mouse_device.cycle = 1;
            break;
        case 1:
            mouse_device.packet.x = (int16_t)(int8_t)(uint8_t)byte;
            mouse_device.cycle = 2;
            break;
        case 2: {
            mouse_device.packet.y = (int16_t)(int8_t)(uint8_t)byte;

            mouse_device.prev_x = mouse_device.x;
            mouse_device.prev_y = mouse_device.y;

            mouse_device.x = (int16_t)(mouse_device.x + mouse_device.packet.x);
            mouse_device.y = (int16_t)(mouse_device.y - mouse_device.packet.y);

            mouse_device.prev_flags   = mouse_device.flags;
            mouse_device.flags        = mouse_device.packet.flags & (MOUSE_LEFT | MOUSE_RIGHT | MOUSE_MIDDLE);
            mouse_device.packet.flags = mouse_device.packet.flags & (MOUSE_LEFT | MOUSE_RIGHT | MOUSE_MIDDLE);

            if (mouse_device.x < 0) {
                mouse_device.x = 0;
            }
            if (mouse_device.y < 0) {
                mouse_device.y = 0;
            }

            struct limine_framebuffer *fb = framebuffer_current();
            if (fb) {
                if (mouse_device.x >= (int16_t)fb->width) {
                    mouse_device.x = (int16_t)((int16_t)fb->width - 1);
                }
                if (mouse_device.y >= (int16_t)fb->height) {
                    mouse_device.y = (int16_t)((int16_t)fb->height - 1);
                }
            }

            mouse_device.packet.x = mouse_device.x;
            mouse_device.packet.y = mouse_device.y;

            uint64_t rflags;
            SPIN_LOCK_INT_SAVE(mouse_lock, rflags);
            mouse_buffer_push(mouse_device.packet);
            thread_wakeup(&mouse_device);
            SPIN_UNLOCK_INT_RESTORE(mouse_lock, rflags);

            mouse_device.cycle = 0;
            break;
        }
        default:
            mouse_device.cycle = 0;
            break;
        }

        status = inb(MOUSE_STATUS);
    }

    apic_send_eoi();
}

void mouse_init(void)
{
    spinlock_init(&mouse_lock);
    mouse_buf_head = mouse_buf_tail = 0;

    mouse_wait(1);
    outb(MOUSE_STATUS, 0xA8);
    mouse_wait(1);
    outb(MOUSE_STATUS, 0x20);
    mouse_wait(0);
    uint8_t status = inb(0x60);
    status         |= 0x03;  // Enable both keyboard (bit 0) and mouse (bit 1) interrupts
    status         |= 0x40;  // Ensure translation stays enabled (bit 6)
    status         &= ~0x20; // Enable mouse clock (bit 5 = 0 means enabled)
    mouse_wait(1);
    outb(MOUSE_STATUS, 0x60);
    mouse_wait(1);
    outb(MOUSE_PORT, status);
    // Set defaults and put mouse in stream mode, then enable data reporting
    mouse_device_write(MOUSE_SET_DEFAULTS);
    mouse_device_read();
    mouse_device_write(MOUSE_SET_STREAM_MODE);
    mouse_device_read();
    mouse_device_write(MOUSE_ENABLE_DATA_REPORTING);
    mouse_device_read();

    constexpr uint8_t vector = IRQ_BASE + IRQ_MOUSE;
    apic_enable_irq(IRQ_MOUSE, vector);
    register_interrupt_handler(vector, mouse_handler);

    mouse_device.initialized = 1;

    vfs_inode_t *node = kmalloc(sizeof(vfs_inode_t));
    if (!node)
        return;

    memset(node, 0, sizeof(vfs_inode_t));
    node->flags = VFS_CHARDEVICE;
    node->iops  = &mouse_dev_ops;

    devfs_register_device("mouse", node);
}

void mouse_get_position(mouse_t *mouse)
{
    if (!mouse)
        return;

    if (mouse_device.received) {
        return;
    }

    mouse->x     = mouse_device.x;
    mouse->y     = mouse_device.y;
    mouse->flags = mouse_device.flags;

    mouse_device.received = 1;
}

/** @brief Inject a relative mouse movement event into the input subsystem. */
void mouse_inject_event(int16_t dx, int16_t dy, uint8_t buttons)
{
    mouse_device.prev_x     = mouse_device.x;
    mouse_device.prev_y     = mouse_device.y;
    mouse_device.prev_flags = mouse_device.flags;
    mouse_device.received   = 0;

    mouse_device.x = (int16_t)(mouse_device.x + dx);
    mouse_device.y = (int16_t)(mouse_device.y + dy);
    mouse_device.flags = buttons & (MOUSE_LEFT | MOUSE_RIGHT | MOUSE_MIDDLE);

    if (mouse_device.x < 0)
        mouse_device.x = 0;
    if (mouse_device.y < 0)
        mouse_device.y = 0;

    struct limine_framebuffer *fb = framebuffer_current();
    if (fb) {
        if (mouse_device.x >= (int16_t)fb->width)
            mouse_device.x = (int16_t)((int16_t)fb->width - 1);
        if (mouse_device.y >= (int16_t)fb->height)
            mouse_device.y = (int16_t)((int16_t)fb->height - 1);
    }

    struct ps2_mouse_packet pkt = {
        .flags = mouse_device.flags,
        .x     = mouse_device.x,
        .y     = mouse_device.y,
    };

    uint64_t rflags;
    SPIN_LOCK_INT_SAVE(mouse_lock, rflags);
    mouse_buffer_push(pkt);
    thread_wakeup(&mouse_device);
    SPIN_UNLOCK_INT_RESTORE(mouse_lock, rflags);
}

/** @brief Set the mouse cursor to an absolute screen position. */
void mouse_set_absolute(int16_t x, int16_t y, uint8_t buttons)
{
    mouse_device.prev_x     = mouse_device.x;
    mouse_device.prev_y     = mouse_device.y;
    mouse_device.prev_flags = mouse_device.flags;
    mouse_device.received   = 0;

    mouse_device.x     = x;
    mouse_device.y     = y;
    mouse_device.flags = buttons & (MOUSE_LEFT | MOUSE_RIGHT | MOUSE_MIDDLE);

    if (mouse_device.x < 0)
        mouse_device.x = 0;
    if (mouse_device.y < 0)
        mouse_device.y = 0;

    struct limine_framebuffer *fb = framebuffer_current();
    if (fb) {
        if (mouse_device.x >= (int16_t)fb->width)
            mouse_device.x = (int16_t)((int16_t)fb->width - 1);
        if (mouse_device.y >= (int16_t)fb->height)
            mouse_device.y = (int16_t)((int16_t)fb->height - 1);
    }

    struct ps2_mouse_packet pkt = {
        .flags = mouse_device.flags,
        .x     = mouse_device.x,
        .y     = mouse_device.y,
    };

    uint64_t rflags;
    SPIN_LOCK_INT_SAVE(mouse_lock, rflags);
    mouse_buffer_push(pkt);
    thread_wakeup(&mouse_device);
    SPIN_UNLOCK_INT_RESTORE(mouse_lock, rflags);
}

void mouse_flush_pending_events(void)
{
    uint64_t rflags;
    SPIN_LOCK_INT_SAVE(mouse_lock, rflags);
    mouse_buf_head = mouse_buf_tail = 0;
    SPIN_UNLOCK_INT_RESTORE(mouse_lock, rflags);
}

static struct inode_operations mouse_dev_ops = {
    .read = mouse_dev_read,
};