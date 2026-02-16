#include <drivers/uart.h>
#include <arch/x86_64/port_io.h>

#define COM1 0x3F8

#define UART_DATA_REG (COM1 + 0)
#define UART_IER_REG (COM1 + 1)
#define UART_IIR_REG (COM1 + 2)
#define UART_FCR_REG (COM1 + 2)
#define UART_LCR_REG (COM1 + 3)
#define UART_MCR_REG (COM1 + 4)
#define UART_LSR_REG (COM1 + 5)
#define UART_MSR_REG (COM1 + 6)
#define UART_SCR_REG (COM1 + 7)

#define UART_LCR_DLAB 0x80
#define UART_LCR_8BIT 0x03
#define UART_FCR_ENABLE 0x01
#define UART_FCR_CLEAR_RX 0x02
#define UART_FCR_CLEAR_TX 0x04
#define UART_FCR_TRIGGER_14 0xC0
#define UART_MCR_DTR 0x01
#define UART_MCR_RTS 0x02
#define UART_MCR_OUT2 0x08
#define UART_LSR_DR 0x01
#define UART_LSR_THRE 0x20

#define UART_DIVISOR_38400 0x03
#define UART_TEST_INPUT_BUFFER_SIZE 256

static bool uart_present = false;

static volatile uint32_t uart_test_read_ptr  = 0;
static volatile uint32_t uart_test_write_ptr = 0;
static char uart_test_input_buffer[UART_TEST_INPUT_BUFFER_SIZE];

static bool uart_pop_test_char(char *out)
{
    if (uart_test_read_ptr == uart_test_write_ptr)
        return false;
    *out              = uart_test_input_buffer[uart_test_read_ptr];
    uart_test_read_ptr = (uart_test_read_ptr + 1) % UART_TEST_INPUT_BUFFER_SIZE;
    return true;
}

void uart_init(void)
{
    // Scratch register test: detect whether a UART chip is present at COM1.
    // Without this, machines lacking a serial port return 0xFF for all port
    // reads, which makes uart_has_rx() permanently true and floods console
    // input with garbage bytes.
    outb(UART_SCR_REG, 0xA5);
    if (inb(UART_SCR_REG) != 0xA5) {
        uart_present = false;
        return;
    }

    outb(UART_IER_REG, 0x00);                // Disable all interrupts
    outb(UART_LCR_REG, UART_LCR_DLAB);       // Enable DLAB (set baud rate divisor)
    outb(UART_DATA_REG, UART_DIVISOR_38400); // Set divisor to 3 (lo byte) 38,400 baud
    outb(UART_IER_REG, 0x00);                //                  (hi byte)
    outb(UART_LCR_REG, UART_LCR_8BIT);       // 8 bits, no parity, one stop bit
    outb(UART_FCR_REG, UART_FCR_ENABLE | UART_FCR_CLEAR_RX | UART_FCR_CLEAR_TX | UART_FCR_TRIGGER_14);
    // Enable FIFO, clear them, with a 14-byte threshold
    outb(UART_MCR_REG, UART_MCR_OUT2 | UART_MCR_RTS | UART_MCR_DTR); // IRQs enabled, RTS/DSR set
    uart_present = true;
}

int uart_is_transmit_empty(void)
{
    return inb(UART_LSR_REG) & UART_LSR_THRE;
}

void uart_putc(char c)
{
    while (uart_is_transmit_empty() == 0) {}
    outb(UART_DATA_REG, c);
}

void uart_puts(const char *str)
{
    while (*str) {
        uart_putc(*str++);
    }
}

bool uart_has_rx(void)
{
    if (uart_test_read_ptr != uart_test_write_ptr)
        return true;
    if (!uart_present)
        return false;
    return (inb(UART_LSR_REG) & UART_LSR_DR) != 0;
}

bool uart_try_getc(char *out)
{
    if (!out)
        return false;
    if (uart_pop_test_char(out))
        return true;
    if (!uart_present)
        return false;
    if ((inb(UART_LSR_REG) & UART_LSR_DR) == 0)
        return false;
    *out = (char)inb(UART_DATA_REG);
    return true;
}

void uart_inject_input_for_test(char c)
{
    uint32_t next = (uart_test_write_ptr + 1) % UART_TEST_INPUT_BUFFER_SIZE;
    if (next == uart_test_read_ptr)
        uart_test_read_ptr = (uart_test_read_ptr + 1) % UART_TEST_INPUT_BUFFER_SIZE;
    uart_test_input_buffer[uart_test_write_ptr] = c;
    uart_test_write_ptr                          = next;
}

void uart_reset_input_for_test(void)
{
    uart_test_read_ptr  = 0;
    uart_test_write_ptr = 0;
}
