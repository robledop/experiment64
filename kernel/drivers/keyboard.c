#include "keyboard.h"
#include "io.h"
#include "pic.h"
#include "terminal.h"

#define KEYBOARD_DATA_PORT 0x60

// US QWERTY Scancode Set 1
static char scancode_to_char[] = {
    0, 27, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',
    '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',
    0, 'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`', 0,
    '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0, '*',
    0, ' ', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, '-',
    0, 0, 0, '+', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

void keyboard_init(void)
{
}

void keyboard_handler_main(void)
{
    uint8_t scancode = inb(KEYBOARD_DATA_PORT);

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
            if (c)
            {
                printf("%c", c);
            }
        }
    }
}
