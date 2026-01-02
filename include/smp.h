#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <limine.h>

void smp_init_cpu0(void);
void smp_boot_aps(void);
void smp_init_ap_scheduler(void);
void smp_ap_scheduler_ready(void);
uint32_t smp_get_cpu_count(void);
bool smp_is_bsp(void);
