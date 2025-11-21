#include "gdt.h"

struct gdt_desc
{
    uint16_t limit;
    uint16_t base_low;
    uint8_t base_mid;
    uint8_t access;
    uint8_t granularity;
    uint8_t base_high;
} __attribute__((packed));

struct gdt_ptr
{
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

static struct gdt_desc gdt[3];
static struct gdt_ptr gdtp;

void gdt_init(void)
{
    gdtp.limit = sizeof(gdt) - 1;
    gdtp.base = (uint64_t)&gdt;

    // 0: Null Descriptor
    gdt[0] = (struct gdt_desc){0, 0, 0, 0, 0, 0};

    // 1: Kernel Code (0x08)
    // Access: Present(1) | Ring0(00) | Code/Data(1) | Exec(1) | DC(0) | RW(1) | Ac(0) = 10011010 = 0x9A
    // Flags: Gran(0) | Long(1) | Size(0) = 0010 = 0x2
    // Note: In 64-bit mode, Base and Limit are ignored.
    gdt[1] = (struct gdt_desc){0, 0, 0, 0x9A, 0x20, 0};

    // 2: Kernel Data (0x10)
    // Access: Present(1) | Ring0(00) | Code/Data(1) | Exec(0) | DC(0) | RW(1) | Ac(0) = 10010010 = 0x92
    gdt[2] = (struct gdt_desc){0, 0, 0, 0x92, 0x00, 0};

    __asm__ volatile("lgdt %0" : : "m"(gdtp));

    // Reload segments
    // We push the new CS (0x08) and the return address, then do a far return (retfq)
    // to reload CS. DS, ES, FS, GS, SS are loaded with 0x10.
    __asm__ volatile(
        "pushq $0x08\n"
        "leaq 1f(%%rip), %%rax\n"
        "pushq %%rax\n"
        "lretq\n"
        "1:\n"
        "movw $0x10, %%ax\n"
        "movw %%ax, %%ds\n"
        "movw %%ax, %%es\n"
        "movw %%ax, %%fs\n"
        "movw %%ax, %%gs\n"
        "movw %%ax, %%ss\n"
        : : : "rax", "memory");
}
