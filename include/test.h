#pragma once

#include <stdbool.h>
#include "terminal.h"

#define ASSERT(condition)                                                     \
    if (!(condition))                                                         \
    {                                                                         \
        printf("TEST FAILED: %s at %s:%d\n", #condition, __FILE__, __LINE__); \
        return false;                                                         \
    }

#define TEST(name) \
    bool name(void)

typedef bool (*test_func_t)(void);

struct test_case
{
    const char *name;
    test_func_t func;
};

void run_tests(void);
