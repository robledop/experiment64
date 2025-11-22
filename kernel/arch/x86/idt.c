#include "idt.h"
#include "limine.h"
#include <stddef.h>
#include "terminal.h"
#include "keyboard.h"
#include "pic.h"
#include "apic.h"
#include "ide.h"

#define IDT_FLAG_PRESENT 0x80
#define IDT_FLAG_RING0 0x00
#define IDT_FLAG_RING3 0x60
#define IDT_FLAG_INTGATE 0x0E
#define IDT_FLAG_TRAPGATE 0x0F

#define IRQ_BASE 32
#define IRQ_KEYBOARD 1
#define IRQ_IDE_PRIMARY 14
#define IRQ_IDE_SECONDARY 15

// We need the framebuffer request to get the framebuffer
extern volatile struct limine_framebuffer_request framebuffer_request;

struct idt_entry
{
    uint16_t offset_low;
    uint16_t selector;
    uint8_t ist;
    uint8_t type_attr;
    uint16_t offset_mid;
    uint32_t offset_high;
    uint32_t zero;
} __attribute__((packed));

struct idt_ptr
{
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

__attribute__((aligned(0x10))) static struct idt_entry idt[256];
static struct idt_ptr idtr;
static isr_handler_t isr_handlers[256];

extern void *isr_stub_table[];

void idt_set_gate(uint8_t num, uint64_t base, uint16_t sel, uint8_t flags)
{
    idt[num].offset_low = base & 0xFFFF;
    idt[num].offset_mid = (base >> 16) & 0xFFFF;
    idt[num].offset_high = (base >> 32) & 0xFFFFFFFF;
    idt[num].selector = sel;
    idt[num].ist = 0;
    idt[num].type_attr = flags;
    idt[num].zero = 0;
}

void register_interrupt_handler(uint8_t vector, isr_handler_t handler)
{
    isr_handlers[vector] = handler;
}

void register_trap_handler(uint8_t vector, isr_handler_t handler)
{
    isr_handlers[vector] = handler;
    idt_set_gate(vector, (uint64_t)isr_stub_table[vector], 0x08, IDT_FLAG_PRESENT | IDT_FLAG_RING3 | IDT_FLAG_TRAPGATE);
}

static void keyboard_isr(struct interrupt_frame *frame)
{
    (void)frame;
    keyboard_handler_main();
}

static void ide_primary_isr(struct interrupt_frame *frame)
{
    (void)frame;
    ide_irq_handler(0);
}

static void ide_secondary_isr(struct interrupt_frame *frame)
{
    (void)frame;
    ide_irq_handler(1);
}

void interrupt_handler(struct interrupt_frame *frame)
{
    if (isr_handlers[frame->int_no])
    {
        isr_handlers[frame->int_no](frame);
    }
    else if (frame->int_no < 32)
    {
        struct limine_framebuffer *fb = framebuffer_request.response->framebuffers[0];

        // Clear screen to red
        for (size_t y = 0; y < fb->height; y++)
        {
            uint32_t *fb_ptr = (uint32_t *)((uint8_t *)fb->address + y * fb->pitch);
            for (size_t x = 0; x < fb->width; x++)
            {
                fb_ptr[x] = 0xFF880000; // Red
            }
        }

        terminal_init(fb);
        terminal_set_cursor(10, 10);
        terminal_set_color(0xFFFFFFFF);
        printf("PANIC: EXCEPTION OCCURRED! Vector: %d\n", frame->int_no);
        printf("Error Code: 0x%lx\n", frame->err_code);
        printf("RIP: 0x%lx\n", frame->rip);
        printf("CS: 0x%lx\n", frame->cs);
        printf("RFLAGS: 0x%lx\n", frame->rflags);
        printf("RSP: 0x%lx\n", frame->rsp);
        printf("SS: 0x%lx\n", frame->ss);

        if (frame->int_no == 14)
        {
            uint64_t cr2;
            __asm__ volatile("mov %%cr2, %0" : "=r"(cr2));
            printf("CR2 (Page Fault Address): 0x%lx\n", cr2);
        }

        for (;;)
        {
            __asm__("hlt");
        }
    }

    if (frame->int_no >= 32)
    {
        apic_send_eoi();
    }
}

void idt_init(void)
{
    idtr.limit = sizeof(idt) - 1;
    idtr.base = (uint64_t)&idt;

    for (int i = 0; i < 256; i++)
    {
        // Use Interrupt Gates (0x0E) for all entries to automatically disable interrupts
        // upon entry. This avoids the need for manual 'cli' instructions in handlers.
        // If we wanted to allow nested interrupts (e.g. for system calls or non-critical exceptions),
        // we would use Trap Gates (0x0F) here.
        idt_set_gate(i, (uint64_t)isr_stub_table[i], 0x08, IDT_FLAG_PRESENT | IDT_FLAG_RING0 | IDT_FLAG_INTGATE);
        isr_handlers[i] = NULL;
    }

    register_interrupt_handler(IRQ_BASE + IRQ_KEYBOARD, keyboard_isr);
    register_interrupt_handler(IRQ_BASE + IRQ_IDE_PRIMARY, ide_primary_isr);
    register_interrupt_handler(IRQ_BASE + IRQ_IDE_SECONDARY, ide_secondary_isr);

    __asm__ volatile("lidt %0" : : "m"(idtr));
    __asm__ volatile("sti");
}
