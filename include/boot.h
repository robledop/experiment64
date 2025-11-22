#pragma once
#include <stdint.h>

void boot_init(void);
void boot_init_terminal(void);
uint64_t boot_get_hhdm_offset(void);
