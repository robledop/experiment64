#include "keyboard.h"
#include "io.h"
#include "process.h"
#include "cpu.h"
#include "devfs.h"
#include "vfs.h"
#include "heap.h"
#include "string.h"

#define KEYBOARD_DATA_PORT 0x60

#define SCANCODE_LSHIFT_PRESS 0x2A
#define SCANCODE_RSHIFT_PRESS 0x36
#define SCANCODE_LSHIFT_RELEASE 0xAA
#define SCANCODE_RSHIFT_RELEASE 0xB6
#define SCANCODE_LCTRL_PRESS 0x1D
#define SCANCODE_LCTRL_RELEASE 0x9D
#define SCANCODE_LALT_PRESS 0x38
#define SCANCODE_LALT_RELEASE 0xB8
#define SCANCODE_CAPSLOCK_PRESS 0x3A
#define SCANCODE_RELEASE_MASK 0x80
#define SCANCODE_EXTENDED_PREFIX 0xE0

#define BUFFER_SIZE 128
// Volatile is necessary here because these variables are modified by the interrupt handler
// and read by the main thread (sys_read -> keyboard_get_char). Without volatile,
// the compiler might cache the values in registers and not see the updates from the ISR,
// causing the shell to think the buffer is empty (freeze).
static volatile char buffer[BUFFER_SIZE];
static volatile int write_ptr = 0;
static volatile int read_ptr = 0;

#define RAW_BUFFER_SIZE 256
static volatile uint8_t raw_buffer[RAW_BUFFER_SIZE];
static volatile int raw_write_ptr = 0;
static volatile int raw_read_ptr = 0;

static struct inode_operations keyboard_dev_ops;

static thread_t *keyboard_waiter = nullptr;

static bool shift_pressed = false;
static bool ctrl_pressed = false;
static bool alt_pressed = false;
static bool caps_lock = false;
static bool extended_scancode = false;

void keyboard_clear_modifiers(void)
{
    shift_pressed = false;
    ctrl_pressed = false;
    alt_pressed = false;
    extended_scancode = false;
}

static void keyboard_enqueue_raw(uint8_t scancode)
{
    int next = (raw_write_ptr + 1) % RAW_BUFFER_SIZE;
    if (next == raw_read_ptr)
        return; // Drop if full
    raw_buffer[raw_write_ptr] = scancode;
    raw_write_ptr = next;
}

static void keyboard_enqueue_char(char c)
{
    const int next = (write_ptr + 1) % BUFFER_SIZE;
    if (next == read_ptr)
        return;
    buffer[write_ptr] = c;
    write_ptr = next;

    if (keyboard_waiter)
    {
        keyboard_waiter->state = THREAD_READY;
        keyboard_waiter = nullptr;
    }
}

static void keyboard_enqueue_sequence(const char* seq, size_t len)
{
    for (size_t i = 0; i < len; i++)
        keyboard_enqueue_char(seq[i]);
}

void keyboard_init(void)
{
    keyboard_reset_state_for_test();

    vfs_inode_t *node = kmalloc(sizeof(vfs_inode_t));
    if (!node)
        return;

    memset(node, 0, sizeof(vfs_inode_t));
    node->flags = VFS_CHARDEVICE;
    node->iops = &keyboard_dev_ops;

    devfs_register_device("keyboard", node);
}

static void keyboard_process_scancode(uint8_t scancode)
{
    keyboard_enqueue_raw(scancode);

    if (scancode == SCANCODE_EXTENDED_PREFIX)
    {
        extended_scancode = true;
        return;
    }

    const bool is_release = (scancode & SCANCODE_RELEASE_MASK) != 0;
    const uint8_t code = scancode & ~SCANCODE_RELEASE_MASK;

    if (extended_scancode)
    {
        extended_scancode = false;

        if (code == 0x1D) // Right Ctrl
        {
            ctrl_pressed = !is_release;
            return;
        }

        if (is_release)
            return;

        switch (code)
        {
        case 0x48: // Up
            keyboard_enqueue_sequence("\x1b[A", 3);
            return;
        case 0x50: // Down
            keyboard_enqueue_sequence("\x1b[B", 3);
            return;
        case 0x4B: // Left
            keyboard_enqueue_sequence("\x1b[D", 3);
            return;
        case 0x4D: // Right
            keyboard_enqueue_sequence("\x1b[C", 3);
            return;
        case 0x47: // Home
            keyboard_enqueue_sequence("\x1b[H", 3);
            return;
        case 0x4F: // End
            keyboard_enqueue_sequence("\x1b[F", 3);
            return;
        case 0x49: // Page Up
            keyboard_enqueue_sequence("\x1b[5~", 4);
            return;
        case 0x51: // Page Down
            keyboard_enqueue_sequence("\x1b[6~", 4);
            return;
        case 0x53: // Delete
            keyboard_enqueue_sequence("\x1b[3~", 4);
            return;
        case 0x52: // Insert
            keyboard_enqueue_sequence("\x1b[2~", 4);
            return;
        default:
            return;
        }
    }
    else
    {
        // Handle right Ctrl release (E0 9D) that sets is_release and clears flag.
        if (code == 0x1D && is_release)
        {
            ctrl_pressed = false;
            return;
        }
    }

    if (code == SCANCODE_LSHIFT_PRESS || code == SCANCODE_RSHIFT_PRESS)
    {
        shift_pressed = !is_release;
        return;
    }
    if (code == SCANCODE_LCTRL_PRESS)
    {
        ctrl_pressed = !is_release;
        return;
    }
    if (code == SCANCODE_LALT_PRESS)
    {
        alt_pressed = !is_release;
        return;
    }
    if (code == SCANCODE_CAPSLOCK_PRESS && !is_release)
    {
        caps_lock = !caps_lock;
        return;
    }

    if (is_release)
    {
        return;
    }

    if (code < sizeof(scancode_to_char))
    {
        bool use_shift = shift_pressed;

        char c = (char)scancode_to_char[code];
        if (caps_lock && c >= 'a' && c <= 'z')
        {
            use_shift = !use_shift;
        }

        if (use_shift)
        {
            c = (char)scancode_to_char_shifted[code];
        }
        else
        {
            c = (char)scancode_to_char[code];
        }

        // Handle Ctrl (e.g. Ctrl+C, Ctrl+L)
        if (ctrl_pressed)
        {
            if (c >= 'a' && c <= 'z')
                c = (char)(c - 'a' + 1);
            else if (c >= 'A' && c <= 'Z')
                c = (char)(c - 'A' + 1);
        }

        if (c)
        {
            // Ctrl+P prints the process/thread list (xv6-style)
            if (c == 0x10)
            {
                process_dump();
                return;
            }

            keyboard_enqueue_char(c);
        }
    }
}

void keyboard_handler_main(void)
{
    uint8_t scancode = inb(KEYBOARD_DATA_PORT);
    keyboard_process_scancode(scancode);
}

void keyboard_inject_scancode(uint8_t scancode)
{
    keyboard_process_scancode(scancode);
}

void keyboard_reset_state_for_test(void)
{
    write_ptr = 0;
    read_ptr = 0;
    raw_write_ptr = 0;
    raw_read_ptr = 0;
    shift_pressed = false;
    ctrl_pressed = false;
    alt_pressed = false;
    caps_lock = false;
    keyboard_waiter = nullptr;
    extended_scancode = false;
}

bool keyboard_has_char(void)
{
    return read_ptr != write_ptr;
}

char keyboard_get_char(void)
{
    while (1)
    {
        // Disable interrupts to check buffer and sleep atomically
        uint64_t rflags;
        __asm__ volatile("pushfq; pop %0; cli" : "=r"(rflags));

        if (read_ptr != write_ptr)
        {
            char c = buffer[read_ptr];
            read_ptr = (read_ptr + 1) % BUFFER_SIZE;
            // Restore interrupts
            if (rflags & RFLAGS_IF)
                __asm__ volatile("sti");
            return c;
        }

        // Buffer empty, sleep
        thread_t *cur = get_current_thread();
        if (cur)
        {
            keyboard_waiter = cur;
            cur->state = THREAD_BLOCKED;
        }

        schedule();

        // Restore interrupts if they were enabled before (schedule restores flags, but just in case)
        // Actually schedule restores flags, so if we entered with cli, we return with cli.
        // We loop back to check buffer.
    }
}

uint64_t keyboard_read_raw(uint8_t *out, uint64_t max)
{
    if (!out || max == 0)
        return 0;

    uint64_t read = 0;
    while (read < max && raw_read_ptr != raw_write_ptr)
    {
        out[read++] = raw_buffer[raw_read_ptr];
        raw_read_ptr = (raw_read_ptr + 1) % RAW_BUFFER_SIZE;
    }
    return read;
}

static uint64_t keyboard_dev_read([[maybe_unused]] const vfs_inode_t *node, uint64_t offset, uint64_t size, uint8_t *buffer)
{
    (void)offset;
    return keyboard_read_raw(buffer, size);
}

static int keyboard_dev_ioctl([[maybe_unused]] vfs_inode_t *node, [[maybe_unused]] int request, [[maybe_unused]] void *arg)
{
    // No special configuration supported yet; accept requests for compatibility.
    return 0;
}

static struct inode_operations keyboard_dev_ops = {
    .read = keyboard_dev_read,
    .ioctl = keyboard_dev_ioctl,
};
