#pragma once

#include <stdbool.h>
#include "terminal.h"

#define ASSERT(condition)                                                                    \
    if (!(condition))                                                                        \
    {                                                                                        \
        printk("\033[31mTEST FAILED: %s at %s:%d\033[0m\n", #condition, __FILE__, __LINE__); \
        return false;                                                                        \
    }

typedef bool (*test_func_t)(void);

struct test_case
{
    const char *name;
    test_func_t func;
    int priority;
    int padding;
    long reserved; // Ensure size is 32 bytes to match section alignment (16)
};

#define TEST_PRIO(name, prio)                                                      \
    bool name(void);                                                               \
    static struct test_case __test_case_##name                                     \
        __attribute__((section(".test_array"), used)) = {#name, name, prio, 0, 0}; \
    bool name(void)

#define TEST(name) TEST_PRIO(name, 100)

void run_tests(void);
void heap_test(void);
bool bio_test(void);
extern volatile const char *g_current_test_name;
