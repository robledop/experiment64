#pragma once

void uart_init(void);
void uart_putc(char c);
void uart_puts(const char *str);
bool uart_has_rx(void);
bool uart_try_getc(char *out);
void uart_inject_input_for_test(char c);
void uart_reset_input_for_test(void);
