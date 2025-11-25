#pragma once

#include <stddef.h>
#include <stdint.h>

// 1 shadow byte covers 2^3 = 8 bytes of real memory.
#define KASAN_SHADOW_SCALE_SHIFT 3
// Place shadow in a canonical high-half region well away from HHDM/kernel.
#define KASAN_SHADOW_OFFSET 0xffff900000000000ULL
#define KASAN_REDZONE_SIZE 16
#define KASAN_POISON_ACCESSIBLE 0x00
#define KASAN_POISON_REDZONE 0xFE
#define KASAN_POISON_FREE 0xFF

#ifdef KASAN

// Compute the shadow address for a real address.
static inline uint8_t* kasan_shadow_for(const void* addr)
{
    return (uint8_t*)(((uintptr_t)addr >> KASAN_SHADOW_SCALE_SHIFT) + KASAN_SHADOW_OFFSET);
}

// Reserve/map shadow and record coverage limits. phys_limit is the max physical
// address we intend to cover (rounded up by the caller).
void kasan_early_init(uint64_t hhdm_offset, uint64_t phys_limit);

// Poison/unpoison helpers for arbitrary ranges.
void kasan_poison_range(const void* addr, size_t size, uint8_t value);
void kasan_unpoison_range(const void* addr, size_t size);

// Validate a range and optionally panic on failure.
bool kasan_check_range(const void* addr, size_t size, bool is_write, const void* ip);
void kasan_report(const void* addr, size_t size, bool is_write, const void* ip);
bool kasan_is_ready(void);
uint8_t kasan_shadow_value(const void* addr);

#else

// No-op stubs when KASAN is disabled.
[[maybe_unused]] static inline uint8_t* kasan_shadow_for([[maybe_unused]] const void* addr)
{
    return nullptr;
}

[[maybe_unused]] static inline void kasan_early_init([[maybe_unused]] uint64_t hhdm_offset,
                                                     [[maybe_unused]] uint64_t phys_limit)
{
}

[[maybe_unused]] static inline void kasan_poison_range([[maybe_unused]] const void* addr, [[maybe_unused]] size_t size,
                                                       [[maybe_unused]] uint8_t value)
{
}

[[maybe_unused]] static inline void kasan_unpoison_range([[maybe_unused]] const void* addr,
                                                         [[maybe_unused]] size_t size)
{
}

[[maybe_unused]] static inline bool kasan_check_range([[maybe_unused]] const void* addr, [[maybe_unused]] size_t size,
                                                      [[maybe_unused]] bool is_write, [[maybe_unused]] const void* ip)
{
    return true;
}

[[maybe_unused]] static inline void kasan_report([[maybe_unused]] const void* addr, [[maybe_unused]] size_t size,
                                                 [[maybe_unused]] bool is_write, [[maybe_unused]] const void* ip)
{
}

[[maybe_unused]] static inline bool kasan_is_ready(void)
{
    return false;
}

[[maybe_unused]] static inline uint8_t kasan_shadow_value([[maybe_unused]] const void* addr)
{
    return 0;
}

#endif
