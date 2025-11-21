#include "idt.h"
#include "limine.h"
#include <stddef.h>
#include "terminal.h"
#include "keyboard.h"
#include "pic.h"
#include "apic.h"

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

void exception_handler(struct interrupt_frame *frame)
{
    // Handle IRQs
    if (frame->int_no >= 32 && frame->int_no < 48)
    {
        if (frame->int_no == 33)
        {
            keyboard_handler_main();
        }

        apic_send_eoi();
        return;
    }

    // Disable interrupts
    __asm__ volatile("cli");

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
    printf("PANIC: EXCEPTION OCCURRED! Vector: %d", frame->int_no);

    // TODO: Print interrupt number and error code
    // Since we don't have printf/itoa yet, we can't easily print numbers.
    // But the red screen confirms the handler ran.

    for (;;)
    {
        __asm__("hlt");
    }
}

void idt_init(void)
{
    idtr.limit = sizeof(idt) - 1;
    idtr.base = (uint64_t)&idt;

    for (int i = 0; i < 256; i++)
    {
        idt_set_gate(i, (uint64_t)isr_stub_table[i], 0x08, 0x8E);
    }

    __asm__ volatile("lidt %0" : : "m"(idtr));
    __asm__ volatile("sti");
}
