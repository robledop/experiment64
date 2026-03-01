#include <stdint.h>

#ifdef TEST_MODE
#include <tests/test.h>
#include <drivers/terminal.h>
#include <arch/x86_64/port_io.h>
#include <lib/string.h>
#include <lib/sort.h>
#include <kernel.h>
#include <drivers/tsc.h>
#include <debug.h>

extern struct test_case __start_test_array[];
extern struct test_case __stop_test_array[];

volatile const char *g_current_test_name = nullptr;
volatile bool g_test_failed = false;
volatile uint64_t g_current_test_start_ns = 0;

#ifndef TEST_PROGRESS_LOGGING
#define TEST_PROGRESS_LOGGING 0
#endif

void test_mark_failure(const char *file, int line, const char *expr)
{
    g_test_failed = true;
    printk("\033[31mTEST ASSERTION FAILED: %s at %s:%d\033[0m\n", expr, file, line);
}

static int compare_tests(const void *a, const void *b)
{
    const struct test_case *ta = *(const struct test_case **)a;
    const struct test_case *tb = *(const struct test_case **)b;

    if (ta->priority < tb->priority)
        return -1;
    if (ta->priority > tb->priority)
        return 1;
    return 0;
}

void run_tests(void)
{
    printk("STARTING TESTS...\n");
    int passed = 0;
    int total = 0;

    uintptr_t start_addr = (uintptr_t)__start_test_array;
    uintptr_t stop_addr = (uintptr_t)__stop_test_array;
    size_t total_size = stop_addr - start_addr;
    size_t count = total_size / sizeof(struct test_case);

    printk("Found %lu tests.\n", count);

    if (count > 512)
    {
        printk("Warning: Too many tests (%lu), capping at 512.\n", count);
        count = 512;
    }

    struct test_case *tests[512];

    // Initialize pointer array
    size_t idx = 0;
    for (struct test_case *t = __start_test_array; t < __stop_test_array; t++)
    {
        tests[idx++] = t;
    }

    // Sort by priority
    qsort(tests, count, sizeof(struct test_case *), compare_tests);

    uint64_t suite_start_ns = tsc_nanos();

    for (size_t i = 0; i < count; i++)
    {
        struct test_case *t = tests[i];
        g_current_test_name = t->name;
        g_test_failed = false;
        g_current_test_start_ns = tsc_nanos();
#if TEST_PROGRESS_LOGGING
        printk("[RUN ] %s\n", t->name);
#endif
        total++;
#ifdef TEST_LOGGING
        printk("[TEST %lu/%lu] Starting: %s (entering...)\n", (unsigned long)(i + 1), (unsigned long)count, t->name);
#endif
        uint64_t test_start_ns = tsc_nanos();
        const bool capture_enabled = true;
        if (capture_enabled)
            test_capture_begin();

        bool ok = t->func();
        if (g_test_failed)
            ok = false;

        if (capture_enabled)
        {
            if (ok)
                test_capture_discard();
            else
                test_capture_flush();
        }
        uint64_t test_end_ns = tsc_nanos();
        uint64_t elapsed_ns = (test_end_ns >= test_start_ns) ? (test_end_ns - test_start_ns) : 0;
        uint64_t elapsed_ms = elapsed_ns / 1000000;
        uint64_t elapsed_us = (elapsed_ns / 1000) % 1000;

        if (ok)
        {
            printk("[\033[32mPASS\033[0m]\033[33m %s \033[0m(%lums %luus)\n", t->name, elapsed_ms, elapsed_us);
            passed++;
        }
        else
        {
            printk("[\033[31mFAIL\033[0m]\033[35m %s \033[0m(%lums %luus)\n", t->name, elapsed_ms, elapsed_us);
            stack_trace();
        }
        g_current_test_name = nullptr;
        g_current_test_start_ns = 0;
    }

    uint64_t suite_end_ns = tsc_nanos();
    uint64_t suite_elapsed_ms = (suite_end_ns > suite_start_ns) ? ((suite_end_ns - suite_start_ns) / 1000000) : 0;

    printk("\nTest Summary: %d/%d passed in %lums.\n", passed, total, suite_elapsed_ms);

    if (passed == total)
    {
        printk("\033[32mALL TESTS PASSED\033[0m\n");
    }
    else
    {
        printk("\033[31mSOME TESTS FAILED\033[0m\n");
    }

    shutdown();

    panic("Failed to exit QEMU via isa-debug-exit");
}
#else
// In non-TEST_MODE builds provide stubs so test macros link.
volatile bool g_test_failed = false;
volatile const char *g_current_test_name = nullptr;
volatile uint64_t g_current_test_start_ns = 0;

void test_mark_failure(const char *file, int line, const char *expr)
{
    (void)file;
    (void)line;
    (void)expr;
}

void run_tests(void)
{
}
#endif
