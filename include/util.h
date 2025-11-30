#pragma once

#include "heap.h"
#include <limits.h>
#include <stdint.h>

typedef void (*defer_func_t)(void *);

struct defer_action
{
    defer_func_t func;
    void *arg;
};

static inline void defer_cleanup(struct defer_action *action)
{
    if (action && action->func)
        action->func(action->arg);
}

#define DEFER_NAME(base, line) base##line
#define DEFER(base, line) DEFER_NAME(base, line)
#define defer(func, arg) __attribute__((cleanup(defer_cleanup))) struct defer_action DEFER(_defer_, __LINE__) = {func, arg}

static inline void cleanup_kfree(void *ptr)
{
    if (!ptr)
    {
        return;
    }
    auto p = (void **)ptr;
    if (*p)
    {
        kfree(*p);
    }
}

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

static inline uint64_t align_up(uint64_t val, uint64_t align)
{
    return (val + align - 1) & ~(align - 1);
}
