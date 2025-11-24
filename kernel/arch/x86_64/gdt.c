#include "gdt.h"
#include "string.h"
#include "cpu.h"

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

void tss_set_stack(uint64_t stack)
{
    cpu_t *cpu = get_cpu();
    cpu->tss.rsp0 = stack;
}

void gdt_init(void)
{
    cpu_t *cpu = get_cpu();
    struct gdt_desc *gdt = cpu->gdt;
    struct tss_entry *tss = &cpu->tss;
    struct gdt_ptr gdtp;

    gdtp.limit = sizeof(struct gdt_desc) * 7 - 1;
    gdtp.base = (uint64_t)gdt;

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
    memset(tss, 0, sizeof(struct tss_entry));
    tss->iomap_base = sizeof(struct tss_entry); // Disable IO Map

    uint64_t tss_base = (uint64_t)tss;
    uint64_t tss_limit = sizeof(struct tss_entry) - 1;

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

    __asm__ volatile(
        "push 0x08\n"
        "lea rax, [rip + 1f]\n"
        "push rax\n"
        "retfq\n"
        "1:\n"
        : : : "rax", "memory");

    __asm__ volatile(
        "mov ax, 0x10\n"
        "mov ds, ax\n"
        "mov es, ax\n"
        "mov ss, ax\n"
        "xor ax, ax\n"
        "mov fs, ax\n"
        "mov gs, ax\n"
        : : : "rax", "memory");

    // Restore GS Base (since loading GS selector might have cleared it)
    wrmsr(MSR_GS_BASE, (uint64_t)cpu);
    wrmsr(MSR_KERNEL_GS_BASE, (uint64_t)cpu);

    // Load TSS
    __asm__ volatile("ltr ax" : : "a"(0x28));
}
