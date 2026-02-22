#pragma once

#include <attributes.h>
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>

static inline USED int clamp_to_int(const uint64_t value)
{
    return (value > (uint64_t)INT_MAX) ? INT_MAX : (int)value;
}

static inline USED int clamp_signed_to_int(int64_t value)
{
    if (value > INT_MAX)
        return INT_MAX;
    if (value < INT_MIN)
        return INT_MIN;
    return (int)value;
}

typedef void (*defer_func_t)(void *);

struct defer_action {
    defer_func_t func;
    void *arg;
};

static inline USED void defer_cleanup(const struct defer_action *action)
{
    if (action && action->func)
        action->func(action->arg);
}

#define DEFER_NAME(base, line) base##line
#define DEFER(base, line) DEFER_NAME(base, line)
#define defer(func, arg)                                                                                               \
    __attribute__((cleanup(defer_cleanup))) struct defer_action DEFER(_defer_, __LINE__) = {func, arg}

static inline USED void cleanup_free(void *ptr)
{
    if (!ptr) {
        return;
    }
    auto p = (void **)ptr;
    if (*p) {
        free(*p);
    }
}

#define CHECK_SUCCESS(expr)                                                                                            \
    do {                                                                                                               \
        if ((expr) != 0)                                                                                               \
            return -1;                                                                                                 \
    } while (0)


#ifndef min
#define min(a, b) ((a) < (b) ? (a) : (b))
#endif

#ifndef max
#define max(a, b) ((a) > (b) ? (a) : (b))
#endif

#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))
