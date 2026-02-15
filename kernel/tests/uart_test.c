#include <tests/test.h>
#include <drivers/console.h>
#include <drivers/keyboard.h>
#include <drivers/uart.h>

#define SC_A 0x1E

TEST(test_uart_input_injection_fifo)
{
    uart_reset_input_for_test();

    uart_inject_input_for_test('x');
    uart_inject_input_for_test('y');

    TEST_ASSERT(uart_has_rx());

    char c = 0;
    TEST_ASSERT(uart_try_getc(&c));
    TEST_ASSERT(c == 'x');
    TEST_ASSERT(uart_try_getc(&c));
    TEST_ASSERT(c == 'y');
    TEST_ASSERT(!uart_try_getc(&c));

    return true;
}

TEST(test_console_reads_keyboard_then_serial)
{
    keyboard_reset_state_for_test();
    uart_reset_input_for_test();

    keyboard_inject_scancode(SC_A);
    uart_inject_input_for_test('z');

    TEST_ASSERT(console_has_char());
    TEST_ASSERT(console_get_char() == 'a');
    TEST_ASSERT(console_get_char() == 'z');
    TEST_ASSERT(!console_has_char());

    return true;
}
