#pragma once

#include <stdint.h>

#define TIMER_FREQUENCY_HZ 50
#define IPI_RESCHEDULE_VECTOR 0xFE

void apic_init(void);
void apic_local_init(void);
void apic_send_eoi(void);
void apic_send_ipi(uint8_t lapic_id, uint8_t vector);
void apic_send_ipi_all_excluding_self(uint8_t vector);
uint32_t apic_get_lapic_id(void);
uint32_t ioapic_read(uint32_t reg);
uint32_t apic_lapic_read(uint32_t reg);
void apic_enable_irq(uint8_t irq, uint8_t vector);
