#pragma once

#include <stdint.h>

static inline uint8_t mmio_read8(uint64_t addr)
{
    return *(volatile uint8_t *)(uintptr_t)addr;
}

static inline uint16_t mmio_read16(uint64_t addr)
{
    return *(volatile uint16_t *)(uintptr_t)addr;
}

static inline uint32_t mmio_read32(uint64_t addr)
{
    return *(volatile uint32_t *)(uintptr_t)addr;
}

static inline uint64_t mmio_read64(uint64_t addr)
{
    return *(volatile uint64_t *)(uintptr_t)addr;
}

static inline void mmio_write8(uint64_t addr, uint8_t data)
{
    *(volatile uint8_t *)(uintptr_t)addr = data;
}

static inline void mmio_write16(uint64_t addr, uint16_t data)
{
    *(volatile uint16_t *)(uintptr_t)addr = data;
}

static inline void mmio_write32(uint64_t addr, uint32_t data)
{
    *(volatile uint32_t *)(uintptr_t)addr = data;
}

static inline void mmio_write64(uint64_t addr, uint64_t data)
{
    *(volatile uint64_t *)(uintptr_t)addr = data;
}
