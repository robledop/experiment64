#pragma once

#include <stdint.h>

static inline uint16_t __bswap_16(uint16_t x)
{
    return (uint16_t)((x >> 8) | (x << 8));
}

static inline uint32_t __bswap_32(uint32_t x)
{
    return (x >> 24) | ((x >> 8) & 0xff00) | ((x << 8) & 0xff0000) | (x << 24);
}

static inline uint64_t __bswap_64(uint64_t x)
{
    return ((uint64_t)__bswap_32((uint32_t)x) << 32) | __bswap_32((uint32_t)(x >> 32));
}

#define bswap_16(x) __bswap_16(x)
#define bswap_32(x) __bswap_32(x)
#define bswap_64(x) __bswap_64(x)
