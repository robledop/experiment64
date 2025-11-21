#include "uart.h"
#include "io.h"

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
#define UART_LSR_THRE 0x20

void uart_init(void)
{
    outb(UART_IER_REG, 0x00);                                                                          // Disable all interrupts
    outb(UART_LCR_REG, UART_LCR_DLAB);                                                                 // Enable DLAB (set baud rate divisor)
    outb(UART_DATA_REG, 0x03);                                                                         // Set divisor to 3 (lo byte) 38400 baud
    outb(UART_IER_REG, 0x00);                                                                          //                  (hi byte)
    outb(UART_LCR_REG, UART_LCR_8BIT);                                                                 // 8 bits, no parity, one stop bit
    outb(UART_FCR_REG, UART_FCR_ENABLE | UART_FCR_CLEAR_RX | UART_FCR_CLEAR_TX | UART_FCR_TRIGGER_14); // Enable FIFO, clear them, with 14-byte threshold
    outb(UART_MCR_REG, UART_MCR_OUT2 | UART_MCR_RTS | UART_MCR_DTR);                                   // IRQs enabled, RTS/DSR set
}

int uart_is_transmit_empty(void)
{
    return inb(UART_LSR_REG) & UART_LSR_THRE;
}

void uart_putc(char c)
{
    while (uart_is_transmit_empty() == 0)
        ;
    outb(UART_DATA_REG, c);
}

void uart_puts(const char *str)
{
    while (*str)
    {
        uart_putc(*str++);
    }
}
