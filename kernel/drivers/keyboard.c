#include "keyboard.h"
#include "io.h"
#include "process.h"
#include "cpu.h"

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

#define BUFFER_SIZE 128
// Volatile is necessary here because these variables are modified by the interrupt handler
// and read by the main thread (sys_read -> keyboard_get_char). Without volatile,
// the compiler might cache the values in registers and not see the updates from the ISR,
// causing the shell to think the buffer is empty (freeze).
static volatile char buffer[BUFFER_SIZE];
static volatile int write_ptr = 0;
static volatile int read_ptr = 0;

static thread_t *keyboard_waiter = nullptr;

static bool shift_pressed = false;
static bool ctrl_pressed = false;
static bool alt_pressed = false;
static bool caps_lock = false;

void keyboard_init(void)
{
}

static void keyboard_process_scancode(uint8_t scancode)
{
    if (scancode == SCANCODE_LSHIFT_PRESS || scancode == SCANCODE_RSHIFT_PRESS)
    {
        shift_pressed = true;
        return;
    }
    if (scancode == SCANCODE_LSHIFT_RELEASE || scancode == SCANCODE_RSHIFT_RELEASE)
    {
        shift_pressed = false;
        return;
    }
    if (scancode == SCANCODE_LCTRL_PRESS)
    {
        ctrl_pressed = true;
        return;
    }
    if (scancode == SCANCODE_LCTRL_RELEASE)
    {
        ctrl_pressed = false;
        return;
    }
    if (scancode == SCANCODE_LALT_PRESS)
    {
        alt_pressed = true;
        return;
    }
    if (scancode == SCANCODE_LALT_RELEASE)
    {
        alt_pressed = false;
        return;
    }
    if (scancode == SCANCODE_CAPSLOCK_PRESS)
    {
        caps_lock = !caps_lock;
        return;
    }

    if (scancode & SCANCODE_RELEASE_MASK)
    {
        return;
    }
    else
    {
        if (scancode < sizeof(scancode_to_char))
        {
            bool use_shift = shift_pressed;

            char c = (char)scancode_to_char[scancode];
            if (caps_lock && c >= 'a' && c <= 'z')
            {
                use_shift = !use_shift;
            }

            if (use_shift)
            {
                c = (char)scancode_to_char_shifted[scancode];
            }
            else
            {
                c = (char)scancode_to_char[scancode];
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

                const int next = (write_ptr + 1) % BUFFER_SIZE;
                if (next != read_ptr)
                {
                    buffer[write_ptr] = c;
                    write_ptr = next;

                    if (keyboard_waiter)
                    {
                        keyboard_waiter->state = THREAD_READY;
                        keyboard_waiter = nullptr;
                    }
                }
            }
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
    shift_pressed = false;
    ctrl_pressed = false;
    alt_pressed = false;
    caps_lock = false;
    keyboard_waiter = nullptr;
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
