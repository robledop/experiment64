#include "keyboard.h"
#include "io.h"
#include "pic.h"
#include "apic.h"
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

static bool wait_input_clear(void)
{
    for (int i = 0; i < 100000; i++)
    {
        if ((inb(0x64) & 0x02) == 0)
            return true;
    }
    return false;
}

static bool wait_output_full(void)
{
    for (int i = 0; i < 100000; i++)
    {
        if (inb(0x64) & 0x01)
            return true;
    }
    return false;
}

static bool controller_write_command(uint8_t cmd)
{
    if (!wait_input_clear())
        return false;
    outb(0x64, cmd);
    return true;
}

static bool controller_write_data(uint8_t data)
{
    if (!wait_input_clear())
        return false;
    outb(0x60, data);
    return true;
}

static int controller_read_data(void)
{
    if (!wait_output_full())
        return -1;
    return inb(0x60);
}

static void flush_output_buffer(void)
{
    while (inb(0x64) & 0x01)
        inb(0x60);
}

static void warn_if_false(bool ok, const char *message)
{
    if (!ok)
        printf("[ WARN ] Keyboard: %s\n", message);
}

void keyboard_init(void)
{
    // Keep the PS/2 controller quiet while we reconfigure it.
    __asm__ volatile("cli");

    warn_if_false(controller_write_command(0xAD), "Failed to disable keyboard port");
    warn_if_false(controller_write_command(0xA7), "Failed to disable mouse port");
    flush_output_buffer();

    warn_if_false(controller_write_command(0x20), "Failed to request config byte");
    int config_read = controller_read_data();
    if (config_read < 0)
        config_read = 0;
    uint8_t config = (uint8_t)config_read;

    config |= 0x01;  // IRQ1 enable
    config |= 0x40;  // Translation to Set 1
    config |= 0x20;  // Disable second port
    config &= ~0x10; // Enable first port clock

    warn_if_false(controller_write_command(0x60), "Failed to select config write");
    warn_if_false(controller_write_data(config), "Failed to write config byte");

    warn_if_false(controller_write_command(0xAA), "Failed to start controller self-test");
    int response = controller_read_data();
    if (response != 0x55)
        printf("[ WARN ] Keyboard: Controller self-test returned %x\n", response);

    warn_if_false(controller_write_command(0xAB), "Failed to test first port");
    response = controller_read_data();
    if (response != 0x00)
        printf("[ WARN ] Keyboard: Keyboard port test returned %x\n", response);

    warn_if_false(controller_write_command(0xAE), "Failed to enable keyboard port");

    warn_if_false(controller_write_data(0xFF), "Failed to send keyboard reset");
    response = controller_read_data();
    if (response != 0xFA)
        printf("[ WARN ] Keyboard: Reset ACK missing (%x)\n", response);
    response = controller_read_data();
    if (response != 0xAA)
        printf("[ WARN ] Keyboard: BAT missing (%x)\n", response);

    warn_if_false(controller_write_data(0xF0), "Failed to select scan code set");
    response = controller_read_data();
    if (response != 0xFA)
        printf("[ WARN ] Keyboard: Scan code select ACK missing (%x)\n", response);
    warn_if_false(controller_write_data(0x01), "Failed to set scan code set 1");
    response = controller_read_data();
    if (response != 0xFA)
        printf("[ WARN ] Keyboard: Scan code value ACK missing (%x)\n", response);

    warn_if_false(controller_write_data(0xF4), "Failed to enable keyboard scanning");
    response = controller_read_data();
    if (response != 0xFA)
        printf("[ WARN ] Keyboard: Enable scanning ACK missing (%x)\n", response);

    flush_output_buffer();

    printf("[ INFO ] Keyboard: Init done. Config: %x\n", config);

    uint32_t low = ioapic_read(0x10 + 2 * 1);
    uint32_t high = ioapic_read(0x10 + 2 * 1 + 1);
    printf("[ INFO ] Keyboard: IOAPIC Redirection Entry 1: %x %x\n", high, low);

    __asm__ volatile("sti");
}

void keyboard_handler_main(void)
{
    uint8_t status = inb(0x64);
    if ((status & 0x01) == 0)
        return;
    uint8_t scancode = inb(KEYBOARD_DATA_PORT);

    uint32_t low = ioapic_read(0x10 + 2 * 1);
    uint32_t high = ioapic_read(0x10 + 2 * 1 + 1);
    printf("KBD: s=%x c=%x IOAPIC=%x %x\n", status, scancode, high, low);

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
