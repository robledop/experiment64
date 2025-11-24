#include "test.h"
#include "terminal.h"
#include "io.h"
#include "string.h"
#include "sort.h"
#include "kernel.h"

extern struct test_case __start_test_array[];
extern struct test_case __stop_test_array[];

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

    if (count > 256)
    {
        printk("Warning: Too many tests (%lu), capping at 256.\n", count);
        count = 256;
    }

    struct test_case *tests[256];

    // Initialize pointer array
    size_t idx = 0;
    for (struct test_case *t = __start_test_array; t < __stop_test_array; t++)
    {
        tests[idx++] = t;
    }

    // Sort by priority
    qsort(tests, count, sizeof(struct test_case *), compare_tests);

    for (size_t i = 0; i < count; i++)
    {
        struct test_case *t = tests[i];
        total++;
        printk("TEST %s: ", t->name);
        if (t->func())
        {
            printk("\033[32mPASSED\033[0m\n");
            passed++;
        }
        else
        {
            printk("\033[31mFAILED\033[0m\n");
        }
    }

    printk("\nTest Summary: %d/%d passed.\n", passed, total);

    if (passed == total)
    {
        printk("\033[32mALL TESTS PASSED\033[0m\n");
    }
    else
    {
        printk("\033[31mSOME TESTS FAILED\033[0m\n");
    }

    // Exit QEMU
    // Try 0x501 which is common default
    outb(ISA_DEBUG_EXIT_PORT, ISA_DEBUG_EXIT_CMD);
    outw(ISA_DEBUG_EXIT_PORT, ISA_DEBUG_EXIT_CMD);
    outd(ISA_DEBUG_EXIT_PORT, ISA_DEBUG_EXIT_CMD);

    // Try 0xf4 as well
    outb(QEMU_EXIT_PORT, QEMU_EXIT_CMD);
    outw(QEMU_EXIT_PORT, QEMU_EXIT_CMD);
    outd(QEMU_EXIT_PORT, QEMU_EXIT_CMD);

    outw(QEMU_SHUTDOWN_PORT, QEMU_SHUTDOWN_CMD);   // qemu
    outw(VBOX_SHUTDOWN_PORT, VBOX_SHUTDOWN_CMD);   // VirtualBox
    outw(BOCHS_SHUTDOWN_PORT, BOCHS_SHUTDOWN_CMD); // Bochs
    outw(CLOUD_SHUTDOWN_PORT, CLOUD_SHUTDOWN_CMD); // Cloud hypervisors

    // If that fails (not in QEMU or device not present), hang.
    printk("Failed to exit QEMU via isa-debug-exit.\n");
    while (1)
        __asm__("hlt");
}
