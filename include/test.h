#pragma once

#include <stdbool.h>
#include "terminal.h"

#define ASSERT(condition)                                                                    \
    if (!(condition))                                                                        \
    {                                                                                        \
        printf("\033[31mTEST FAILED: %s at %s:%d\033[0m\n", #condition, __FILE__, __LINE__); \
        return false;                                                                        \
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
void heap_test(void);
bool bio_test(void);
