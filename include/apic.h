#pragma once

#include <stdint.h>

void apic_init(void);
void apic_local_init(void);
void apic_send_eoi(void);
uint32_t ioapic_read(uint32_t reg);
uint32_t apic_lapic_read(uint32_t reg);
void apic_enable_irq(uint8_t irq, uint8_t vector);
