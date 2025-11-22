#include "gdt.h"
#include "string.h"

#define GDT_ACCESS_PRESENT 0x80
#define GDT_ACCESS_RING0 0x00
#define GDT_ACCESS_RING3 0x60
#define GDT_ACCESS_S 0x10 // 1 for code/data, 0 for system
#define GDT_ACCESS_EXEC 0x08
#define GDT_ACCESS_DC 0x04  // Direction/Conforming
#define GDT_ACCESS_RW 0x02  // Readable/Writable
#define GDT_ACCESS_AC 0x01  // Accessed
#define GDT_ACCESS_TSS 0x09 // Available 64-bit TSS

#define GDT_FLAG_GRAN 0x80
#define GDT_FLAG_SIZE 0x40 // 32-bit
#define GDT_FLAG_LONG 0x20 // 64-bit

struct gdt_desc
{
    uint16_t limit;
    uint16_t base_low;
    uint8_t base_mid;
    uint8_t access;
    uint8_t granularity;
    uint8_t base_high;
} __attribute__((packed));

struct gdt_system_desc
{
    uint16_t limit;
    uint16_t base_low;
    uint8_t base_mid;
    uint8_t access;
    uint8_t granularity;
    uint8_t base_high;
    uint32_t base_upper;
    uint32_t reserved;
} __attribute__((packed));

struct gdt_ptr
{
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

static struct gdt_desc gdt[7];
static struct gdt_ptr gdtp;
static struct tss_entry tss;

void tss_set_stack(uint64_t stack)
{
    tss.rsp0 = stack;
}

void gdt_init(void)
{
    gdtp.limit = sizeof(gdt) - 1;
    gdtp.base = (uint64_t)&gdt;

    // 0: Null Descriptor
    gdt[0] = (struct gdt_desc){0, 0, 0, 0, 0, 0};

    // 1: Kernel Code (0x08)
    // Access: Present | Ring0 | Code/Data | Exec | RW
    // Flags: Long Mode
    gdt[1] = (struct gdt_desc){0, 0, 0,
                               GDT_ACCESS_PRESENT | GDT_ACCESS_RING0 | GDT_ACCESS_S | GDT_ACCESS_EXEC | GDT_ACCESS_RW,
                               GDT_FLAG_LONG,
                               0};

    // 2: Kernel Data (0x10)
    // Access: Present | Ring0 | Code/Data | RW
    gdt[2] = (struct gdt_desc){0, 0, 0,
                               GDT_ACCESS_PRESENT | GDT_ACCESS_RING0 | GDT_ACCESS_S | GDT_ACCESS_RW,
                               0x00,
                               0};

    // 3: User Data (0x18)
    // Access: Present | Ring3 | Code/Data | RW
    gdt[3] = (struct gdt_desc){0, 0, 0,
                               GDT_ACCESS_PRESENT | GDT_ACCESS_RING3 | GDT_ACCESS_S | GDT_ACCESS_RW,
                               0x00,
                               0};

    // 4: User Code (0x20)
    // Access: Present | Ring3 | Code/Data | Exec | RW
    // Flags: Long Mode
    gdt[4] = (struct gdt_desc){0, 0, 0,
                               GDT_ACCESS_PRESENT | GDT_ACCESS_RING3 | GDT_ACCESS_S | GDT_ACCESS_EXEC | GDT_ACCESS_RW,
                               GDT_FLAG_LONG,
                               0};

    // 5 & 6: TSS (0x28)
    memset(&tss, 0, sizeof(tss));
    tss.iomap_base = sizeof(tss); // Disable IO Map

    uint64_t tss_base = (uint64_t)&tss;
    uint64_t tss_limit = sizeof(tss) - 1;

    struct gdt_system_desc *tss_desc = (struct gdt_system_desc *)&gdt[5];
    tss_desc->limit = tss_limit & 0xFFFF;
    tss_desc->base_low = tss_base & 0xFFFF;
    tss_desc->base_mid = (tss_base >> 16) & 0xFF;
    tss_desc->access = GDT_ACCESS_PRESENT | GDT_ACCESS_RING0 | GDT_ACCESS_TSS;
    tss_desc->granularity = 0; // Limit is in bytes
    tss_desc->base_high = (tss_base >> 24) & 0xFF;
    tss_desc->base_upper = (tss_base >> 32) & 0xFFFFFFFF;
    tss_desc->reserved = 0;

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

    // Load TSS
    __asm__ volatile("ltr %%ax" : : "a"(0x28));
}
