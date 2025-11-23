#pragma once

#include <stdint.h>

void tsc_init(void);
uint64_t tsc_get_ticks(void);
uint64_t tsc_get_freq(void);
uint64_t tsc_nanos(void);
void tsc_sleep_ns(uint64_t ns);
void tsc_sleep_ms(uint64_t ms);
