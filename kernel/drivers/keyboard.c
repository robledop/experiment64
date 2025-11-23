#include "keyboard.h"
#include "io.h"
#include "pic.h"
#include "terminal.h"
#include "uart.h"
#include "process.h"
#include <stdbool.h>

#define KEYBOARD_DATA_PORT 0x60

#define BUFFER_SIZE 128
// Volatile is necessary here because these variables are modified by the interrupt handler
// and read by the main thread (sys_read -> keyboard_get_char). Without volatile,
// the compiler might cache the values in registers and not see the updates from the ISR,
// causing the shell to think the buffer is empty (freeze).
static volatile char buffer[BUFFER_SIZE];
static volatile int write_ptr = 0;
static volatile int read_ptr = 0;

static thread_t *keyboard_waiter = NULL;

static bool shift_pressed = false;
static bool ctrl_pressed = false;
static bool alt_pressed = false;
static bool caps_lock = false;

// US QWERTY Scancode Set 1
static char scancode_to_char[] = {
    0, 27, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',
    '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',
    0, 'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`', 0,
    '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0, '*',
    0, ' ', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, '-',
    0, 0, 0, '+', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

static char scancode_to_char_shifted[] = {
    0, 27, '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', '\b',
    '\t', 'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n',
    0, 'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~', 0,
    '|', 'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?', 0, '*',
    0, ' ', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, '-',
    0, 0, 0, '+', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

void keyboard_init(void)
{
}

void keyboard_handler_main(void)
{
    uint8_t scancode = inb(KEYBOARD_DATA_PORT);

    // Handle modifiers
    if (scancode == 0x2A || scancode == 0x36) // Left/Right Shift Press
    {
        shift_pressed = true;
        return;
    }
    if (scancode == 0xAA || scancode == 0xB6) // Left/Right Shift Release
    {
        shift_pressed = false;
        return;
    }
    if (scancode == 0x1D) // Left Ctrl Press
    {
        ctrl_pressed = true;
        return;
    }
    if (scancode == 0x9D) // Left Ctrl Release
    {
        ctrl_pressed = false;
        return;
    }
    if (scancode == 0x38) // Left Alt Press
    {
        alt_pressed = true;
        return;
    }
    if (scancode == 0xB8) // Left Alt Release
    {
        alt_pressed = false;
        return;
    }
    if (scancode == 0x3A) // Caps Lock Press
    {
        caps_lock = !caps_lock;
        return;
    }

    // If the top bit is set, it's a key release
    if (scancode & 0x80)
    {
        // Key release, ignore for now
    }
    else
    {
        // Key press
        if (scancode < sizeof(scancode_to_char))
        {
            char c = scancode_to_char[scancode];
            bool use_shift = shift_pressed;

            // Handle Caps Lock for letters
            char base = scancode_to_char[scancode];
            if (caps_lock && base >= 'a' && base <= 'z')
            {
                use_shift = !use_shift;
            }

            if (use_shift)
            {
                c = scancode_to_char_shifted[scancode];
            }
            else
            {
                c = scancode_to_char[scancode];
            }

            // Handle Ctrl (e.g. Ctrl+C, Ctrl+L)
            if (ctrl_pressed)
            {
                if (c >= 'a' && c <= 'z')
                    c = c - 'a' + 1;
                else if (c >= 'A' && c <= 'Z')
                    c = c - 'A' + 1;
            }

            if (c)
            {
                int next = (write_ptr + 1) % BUFFER_SIZE;
                if (next != read_ptr)
                {
                    buffer[write_ptr] = c;
                    write_ptr = next;

                    if (keyboard_waiter)
                    {
                        keyboard_waiter->state = THREAD_READY;
                        keyboard_waiter = NULL;
                    }
                }
            }
        }
    }
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
            if (rflags & 0x200)
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
