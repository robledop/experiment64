#pragma once

#include <limits.h>
#include <stdint.h>

static inline int clamp_to_int(uint64_t value)
{
    return (value > (uint64_t)INT_MAX) ? INT_MAX : (int)value;
}

static inline int clamp_signed_to_int(int64_t value)
{
    if (value > INT_MAX)
        return INT_MAX;
    if (value < INT_MIN)
        return INT_MIN;
    return (int)value;
}
