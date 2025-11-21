#pragma once

#include <stdint.h>

struct interrupt_frame
{
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
    uint64_t rbp, rdi, rsi, rdx, rcx, rbx, rax;
    uint64_t int_no, err_code;
    uint64_t rip, cs, rflags, rsp, ss;
};

typedef void (*isr_handler_t)(struct interrupt_frame *frame);

void idt_init(void);
void register_interrupt_handler(uint8_t vector, isr_handler_t handler);
void register_trap_handler(uint8_t vector, isr_handler_t handler);
