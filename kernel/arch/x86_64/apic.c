#include "apic.h"
#include "acpi.h"
#include "pic.h"
#include "terminal.h"
#include "cpu.h"
#include "limine.h"
#include <stddef.h>

extern volatile struct limine_hhdm_request hhdm_request;

// Local APIC Registers
#define LAPIC_ID 0x0020
#define LAPIC_EOI 0x00B0
#define LAPIC_SVR 0x00F0
#define LAPIC_ICR0 0x0300
#define LAPIC_ICR1 0x0310
#define LAPIC_LVT_TIMER 0x0320
#define LAPIC_TICR 0x0380
#define LAPIC_TDCR 0x03E0

// IOAPIC Registers
#define IOAPIC_ID 0x00
#define IOAPIC_VER 0x01
#define IOAPIC_ARB 0x02
#define IOAPIC_REDTBL 0x10

static uint64_t lapic_base = 0;
static uint64_t ioapic_base = 0;

#define MAX_ISOS 16
static struct madt_iso isos[MAX_ISOS];
static int iso_count = 0;

static uint32_t apic_get_gsi(uint8_t irq, uint16_t *flags)
{
    for (int i = 0; i < iso_count; i++)
    {
        if (isos[i].irq_source == irq && isos[i].bus_source == 0) // ISA bus is usually 0
        {
            if (flags)
                *flags = isos[i].flags;
            return isos[i].gsi;
        }
    }
    if (flags)
        *flags = 0;
    return irq; // Default 1:1 mapping
}

uint32_t apic_lapic_read(uint32_t reg)
{
    return *((volatile uint32_t *)(lapic_base + reg));
}

static void lapic_write(uint32_t reg, uint32_t value)
{
    *((volatile uint32_t *)(lapic_base + reg)) = value;
}

uint32_t ioapic_read(uint32_t reg)
{
    *((volatile uint32_t *)(ioapic_base)) = reg;
    return *((volatile uint32_t *)(ioapic_base + 0x10));
}

static void ioapic_write(uint32_t reg, uint32_t value)
{
    *((volatile uint32_t *)(ioapic_base)) = reg;
    *((volatile uint32_t *)(ioapic_base + 0x10)) = value;
}

void apic_send_eoi(void)
{
    lapic_write(LAPIC_EOI, 0);
}

void ioapic_set_entry(uint8_t index, uint64_t data)
{
    ioapic_write(IOAPIC_REDTBL + 2 * index, (uint32_t)data);
    ioapic_write(IOAPIC_REDTBL + 2 * index + 1, (uint32_t)(data >> 32));
}

void apic_local_init(void)
{
    // Enable LAPIC
    // Set Spurious Interrupt Vector Register (SVR)
    // Bit 8: Enable APIC
    // Bits 0-7: Spurious Vector (we'll use 0xFF)
    lapic_write(LAPIC_SVR, 0x1FF);

    // Set Task Priority Register (TPR) to 0 (Enable all)
    lapic_write(0x80, 0);

    // Initialize LAPIC Timer
    // Divide by 16
    lapic_write(LAPIC_TDCR, 0x3);
    // Set LVT Timer: Vector 32, Periodic Mode
    lapic_write(LAPIC_LVT_TIMER, 32 | 0x20000);
    // Set Initial Count (approx 10ms on QEMU, need calibration on real hw)
    // Reduced to 100000 to ensure faster context switches during tests
    lapic_write(LAPIC_TICR, 100000);
}

void apic_init(void)
{
    // Disable legacy PIC
    pic_disable();

    struct madt *madt = acpi_find_table("APIC");
    if (madt == NULL)
    {
        boot_message(ERROR, "APIC: MADT not found!");
        return;
    }

    if (hhdm_request.response == NULL)
    {
        boot_message(ERROR, "APIC: HHDM response not found!");
        return;
    }
    uint64_t hhdm_offset = hhdm_request.response->offset;

    lapic_base = madt->local_apic_address + hhdm_offset;
    boot_message(INFO, "APIC: LAPIC base: %lx", lapic_base);

    // Parse MADT entries to find IOAPIC and ISOs
    uint8_t *entry = (uint8_t *)(madt + 1);
    uint8_t *end = (uint8_t *)madt + madt->header.length;

    while (entry < end)
    {
        struct madt_entry_header *header = (struct madt_entry_header *)entry;
        if (header->type == 1) // IOAPIC
        {
            struct madt_ioapic *ioapic = (struct madt_ioapic *)entry;
            ioapic_base = ioapic->ioapic_address + hhdm_offset;
            boot_message(INFO, "APIC: IOAPIC base: %lx", ioapic_base);
        }
        else if (header->type == 2) // ISO
        {
            struct madt_iso *iso = (struct madt_iso *)entry;
            boot_message(INFO, "APIC: ISO bus=%d irq=%d gsi=%d flags=%x", iso->bus_source, iso->irq_source, iso->gsi, iso->flags);
            if (iso_count < MAX_ISOS)
            {
                isos[iso_count++] = *iso;
            }
        }
        entry += header->length;
    }

    apic_local_init();

    // Get current LAPIC ID
    uint32_t lapic_id = apic_lapic_read(LAPIC_ID) >> 24;

    // Map Keyboard (IRQ 1) to Vector 33 (0x21)
    uint16_t kbd_flags = 0;
    uint32_t kbd_gsi = apic_get_gsi(1, &kbd_flags);

    uint64_t entry_val = 33; // Vector 33

    boot_message(INFO, "APIC: Keyboard GSI=%d Flags=%x", kbd_gsi, kbd_flags);

    // Handle flags (Polarity and Trigger Mode)
    // Flags: Bits 0-1 Polarity, Bits 2-3 Trigger Mode
    // Polarity: 00=Bus Conforming, 01=Active High, 11=Active Low
    // Trigger: 00=Bus Conforming, 01=Edge, 11=Level

    if ((kbd_flags & 0x3) == 0x3) // Active Low
        entry_val |= (1 << 13);
    if ((kbd_flags & 0xC) == 0xC) // Level Trigger
        entry_val |= (1 << 15);

    entry_val |= ((uint64_t)lapic_id << 56); // Destination APIC ID

    ioapic_set_entry(kbd_gsi, entry_val);

    boot_message(INFO, "APIC: Initialized.");
}

void apic_enable_irq(uint8_t irq, uint8_t vector)
{
    uint16_t flags = 0;
    uint32_t gsi = apic_get_gsi(irq, &flags);

    uint64_t entry_val = vector;

    // Handle flags (Polarity and Trigger Mode)
    if ((flags & 0x3) == 0x3) // Active Low
        entry_val |= (1 << 13);
    if ((flags & 0xC) == 0xC) // Level Trigger
        entry_val |= (1 << 15);

    entry_val |= ((uint64_t)0 << 56); // Destination APIC ID 0
    ioapic_set_entry(gsi, entry_val);
}
