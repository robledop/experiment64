#include "keyboard.h"
#include "io.h"
#include "pic.h"
#include "terminal.h"
#include <stdbool.h>

#define KEYBOARD_DATA_PORT 0x60

#define BUFFER_SIZE 128
static char buffer[BUFFER_SIZE];
static int write_ptr = 0;
static int read_ptr = 0;

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
            char c = 0;
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
                }
            }
        }
    }
}

char keyboard_get_char(void)
{
    if (read_ptr == write_ptr)
        return 0;
    char c = buffer[read_ptr];
    read_ptr = (read_ptr + 1) % BUFFER_SIZE;
    return c;
}
