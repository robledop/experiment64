#include "test.h"
#include "terminal.h"
#include "io.h"

bool test_strlen(void);
bool test_strcmp(void);
bool test_strncmp(void);
bool test_memcpy(void);
bool test_memset(void);
bool test_memcmp(void);
bool test_pmm_alloc_free(void);
bool test_vmm_map(void);
bool test_ide_read_write(void);
bool test_kmalloc_small(void);
bool test_kmalloc_large(void);
bool test_kzalloc(void);
bool test_krealloc(void);

struct test_case tests[] = {
    {"strlen", test_strlen},
    {"strcmp", test_strcmp},
    {"strncmp", test_strncmp},
    {"memcpy", test_memcpy},
    {"memset", test_memset},
    {"memcmp", test_memcmp},
    {"pmm_alloc_free", test_pmm_alloc_free},
    {"vmm_map", test_vmm_map},
    {"ide_read_write", test_ide_read_write},
    {"kmalloc_small", test_kmalloc_small},
    {"kmalloc_large", test_kmalloc_large},
    {"kzalloc", test_kzalloc},
    {"krealloc", test_krealloc},
    {NULL, NULL}};

void run_tests(void)
{
    printf("STARTING TESTS...\n");
    int passed = 0;
    int total = 0;

    for (int i = 0; tests[i].name != NULL; i++)
    {
        total++;
        printf("TEST %s: ", tests[i].name);
        if (tests[i].func())
        {
            printf("PASSED\n");
            passed++;
        }
        else
        {
            printf("FAILED\n");
        }
    }

    printf("\nTest Summary: %d/%d passed.\n", passed, total);

    if (passed == total)
    {
        printf("ALL TESTS PASSED\n");
    }
    else
    {
        printf("SOME TESTS FAILED\n");
    }

    // Exit QEMU
    // Try 0x501 which is common default
    outb(0x501, 0x10);
    outw(0x501, 0x10);
    outd(0x501, 0x10);

    // Try 0xf4 as well
    outb(0xf4, 0x10);
    outw(0xf4, 0x10);
    outd(0xf4, 0x10);

    // If that fails (not in QEMU or device not present), hang.
    printf("Failed to exit QEMU via isa-debug-exit.\n");
    while (1)
        __asm__("hlt");
}
