#ifdef TEST_MODE
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
bool test_fat32_init(void);
bool test_fat32_list(void);
bool test_fat32_read_file(void);
bool test_fat32_stat(void);
bool test_fat32_write_delete(void);
bool test_fat32_directories(void);
bool test_fat32_stress(void);
bool bio_test(void);
bool test_gpt_enumeration(void);
bool test_vfs_basic(void);
bool test_vfs_open(void);
bool test_vfs_write(void);
bool test_vfs_read(void);
bool test_vfs_close(void);
bool test_syscall_write_exit(void);
bool test_syscall_getpid(void);
bool test_syscall_yield(void);
bool test_syscall_spawn(void);
bool test_syscall_fork(void);
bool test_syscall_sbrk(void);
bool test_syscall_file_io(void);
bool test_syscall_chdir(void);
bool test_syscall_sleep(void);
bool test_syscall_exec(void);
bool test_process_creation(void);
bool test_scheduler(void);

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
    {"fat32_init", test_fat32_init},
    {"fat32_list", test_fat32_list},
    {"fat32_read_file", test_fat32_read_file},
    {"fat32_stat", test_fat32_stat},
    {"fat32_write_delete", test_fat32_write_delete},
    {"fat32_directories", test_fat32_directories},
    // {"fat32_stress", test_fat32_stress},
    {"bio_test", bio_test},
    {"gpt_enumeration", test_gpt_enumeration},
    {"vfs_basic", test_vfs_basic},
    {"vfs_open", test_vfs_open},
    {"vfs_write", test_vfs_write},
    {"vfs_read", test_vfs_read},
    {"vfs_close", test_vfs_close},
    {"process_creation", test_process_creation},
    {"syscall_write_exit", test_syscall_write_exit},
    {"syscall_getpid", test_syscall_getpid},
    {"syscall_yield", test_syscall_yield},
    {"syscall_spawn", test_syscall_spawn},
    {"syscall_fork", test_syscall_fork},
    {"syscall_sbrk", test_syscall_sbrk},
    {"syscall_file_io", test_syscall_file_io},
    {"syscall_chdir", test_syscall_chdir},
    {"syscall_sleep", test_syscall_sleep},
    {"syscall_exec", test_syscall_exec},
    {"scheduler", test_scheduler},
    {0, 0}};

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
            printf("\033[32mPASSED\033[0m\n");
            passed++;
        }
        else
        {
            printf("\033[31mFAILED\033[0m\n");
        }
    }

    printf("\nTest Summary: %d/%d passed.\n", passed, total);

    if (passed == total)
    {
        printf("\033[32mALL TESTS PASSED\033[0m\n");
    }
    else
    {
        printf("\033[31mSOME TESTS FAILED\033[0m\n");
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
#endif // TEST_MODE