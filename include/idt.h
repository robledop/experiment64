#pragma once

#include <stdint.h>

#define IRQ_BASE 32
#define IRQ_KEYBOARD 1
#define IRQ_MOUSE 12
#define IRQ_IDE_PRIMARY 14
#define IRQ_IDE_SECONDARY 15

struct interrupt_frame
{
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
    uint64_t rbp, rdi, rsi, rdx, rcx, rbx, rax;
    uint64_t int_no, err_code;
    uint64_t rip, cs, rflags, rsp, ss;
};

typedef void (*isr_handler_t)(struct interrupt_frame *frame);

void idt_init(void);
void idt_reload(void);
void register_interrupt_handler(uint8_t vector, isr_handler_t handler);
void register_trap_handler(uint8_t vector, isr_handler_t handler);
