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

__attribute__((unused)) static uint32_t lapic_read(uint32_t reg)
{
    return *((volatile uint32_t *)(lapic_base + reg));
}

static void lapic_write(uint32_t reg, uint32_t value)
{
    *((volatile uint32_t *)(lapic_base + reg)) = value;
}

__attribute__((unused)) static uint32_t ioapic_read(uint32_t reg)
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

void apic_init(void)
{
    // Disable legacy PIC
    pic_disable();

    struct madt *madt = acpi_find_table("APIC");
    if (madt == NULL)
    {
        printf("APIC: MADT not found!\n");
        return;
    }

    if (hhdm_request.response == NULL)
    {
        printf("APIC: HHDM response not found!\n");
        return;
    }
    uint64_t hhdm_offset = hhdm_request.response->offset;

    lapic_base = madt->local_apic_address + hhdm_offset;
    printf("APIC: LAPIC base: %lx\n", lapic_base);

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
            printf("APIC: IOAPIC base: %lx\n", ioapic_base);
        }
        else if (header->type == 2) // ISO
        {
            struct madt_iso *iso = (struct madt_iso *)entry;
            printf("APIC: ISO bus=%d irq=%d gsi=%d flags=%x\n", iso->bus_source, iso->irq_source, iso->gsi, iso->flags);
            // TODO: Handle ISOs properly (e.g. active low/high, edge/level)
            // For now, we assume standard ISA overrides usually map IRQ 1 -> GSI 1
        }
        entry += header->length;
    }

    // Enable LAPIC
    // Set Spurious Interrupt Vector Register (SVR)
    // Bit 8: Enable APIC
    // Bits 0-7: Spurious Vector (we'll use 0xFF)
    lapic_write(LAPIC_SVR, 0x1FF);

    // Map Keyboard (IRQ 1) to Vector 33 (0x21)
    // Delivery Mode: Fixed (000)
    // Destination Mode: Physical (0)
    // Pin Polarity: Active High (0) - Default for ISA
    // Trigger Mode: Edge (0) - Default for ISA
    // Masked: 0
    // Destination: LAPIC ID (we'll assume 0 for BSP or broadcast)

    // Note: On real hardware, we must respect ISOs.
    // QEMU usually maps IRQ 1 to GSI 1 directly.

    uint64_t entry_val = 33; // Vector 33
    // entry_val |= (0 << 8); // Fixed delivery
    // entry_val |= (0 << 11); // Physical destination
    // entry_val |= (0 << 13); // Active High
    // entry_val |= (0 << 15); // Edge Trigger
    // entry_val |= (0 << 16); // Unmasked
    entry_val |= ((uint64_t)0 << 56); // Destination APIC ID 0

    ioapic_set_entry(1, entry_val);

    printf("APIC: Initialized.\n");
}

void apic_enable_irq(uint8_t irq, uint8_t vector)
{
    uint64_t entry_val = vector;
    entry_val |= ((uint64_t)0 << 56); // Destination APIC ID 0
    ioapic_set_entry(irq, entry_val);
}
