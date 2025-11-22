#include "keyboard.h"
#include "io.h"
#include "pic.h"
#include "terminal.h"

#define KEYBOARD_DATA_PORT 0x60

#define BUFFER_SIZE 128
static char buffer[BUFFER_SIZE];
static int write_ptr = 0;
static int read_ptr = 0;

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
