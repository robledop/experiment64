#pragma once

#include <stdint.h>

void apic_init(void);
void apic_send_eoi(void);
void apic_enable_irq(uint8_t irq, uint8_t vector);
