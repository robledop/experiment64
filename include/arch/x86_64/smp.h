#pragma once
#include <stdint.h>
#include <arch/x86_64/cpu.h>

#define MAX_CPUS 32

void smp_init_cpu0(void);
void smp_boot_aps(void);
void smp_init_ap_scheduler(void);
void smp_ap_scheduler_ready(void);
uint32_t smp_get_cpu_count(void);
cpu_t *smp_get_cpu_by_index(uint32_t idx);
bool smp_is_bsp(void);
