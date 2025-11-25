#include "test.h"
#include "keyboard.h"
#include "string.h"

// Helper scancodes (set 1)
#define SC_A 0x1E
#define SC_B 0x30
#define SC_C 0x2E
#define SC_SHIFT_PRESS 0x2A
#define SC_SHIFT_RELEASE 0xAA
#define SC_CTRL_PRESS 0x1D
#define SC_CTRL_RELEASE 0x9D
#define SC_CAPSLOCK 0x3A

TEST(test_keyboard_basic_and_modifiers)
{
    keyboard_reset_state_for_test();

    // Simple characters
    keyboard_inject_scancode(SC_A);
    keyboard_inject_scancode(SC_B);
    TEST_ASSERT(keyboard_get_char() == 'a');
    TEST_ASSERT(keyboard_get_char() == 'b');

    // CapsLock toggles case for letters.
    keyboard_inject_scancode(SC_CAPSLOCK);
    keyboard_inject_scancode(SC_A);
    TEST_ASSERT(keyboard_get_char() == 'A');

    // Shift + letter -> uppercase
    keyboard_inject_scancode(SC_SHIFT_PRESS);
    keyboard_inject_scancode(SC_B);
    keyboard_inject_scancode(SC_SHIFT_RELEASE);
    TEST_ASSERT(keyboard_get_char() == 'b'); // caps + shift cancels to lowercase

    // Ctrl + letter -> control code
    keyboard_inject_scancode(SC_CTRL_PRESS);
    keyboard_inject_scancode(SC_C);
    keyboard_inject_scancode(SC_CTRL_RELEASE);
    TEST_ASSERT(keyboard_get_char() == 3); // CTRL+C

    return true;
}

TEST(test_keyboard_buffer_wraparound)
{
    keyboard_reset_state_for_test();

    // Fill buffer with 100 'a's
    for (int i = 0; i < 100; i++)
    {
        keyboard_inject_scancode(SC_A);
    }
    // Drain half
    for (int i = 0; i < 50; i++)
    {
        TEST_ASSERT(keyboard_get_char() == 'a');
    }

    // Add another 60 'b's to wrap write pointer
    for (int i = 0; i < 60; i++)
    {
        keyboard_inject_scancode(SC_B);
    }

    // Consume remaining 110 chars: 50 'a' + 60 'b'
    for (int i = 0; i < 50; i++)
    {
        TEST_ASSERT(keyboard_get_char() == 'a');
    }
    for (int i = 0; i < 60; i++)
    {
        TEST_ASSERT(keyboard_get_char() == 'b');
    }

    TEST_ASSERT(!keyboard_has_char());
    return true;
}
