#pragma once

#include <stdint.h>

// 512-byte buffer for FXSAVE/FXRSTOR, must be 16-byte aligned
typedef struct
{
    uint8_t data[512];
} __attribute__((aligned(16))) fpu_state_t;

void enable_sse(void);
void save_fpu_state(fpu_state_t *state);
void restore_fpu_state(fpu_state_t *state);
void hcf(void);
