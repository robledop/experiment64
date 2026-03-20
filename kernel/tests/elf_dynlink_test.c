#include <tests/test.h>
#include <lib/elf.h>
#include <mem/vmm.h>
#include <lib/string.h>
#include <sys/syscall.h>
#include <task/process.h>

/**
 * @brief Verify elf_load_ex detects PT_INTERP in a dynamically linked binary.
 */
TEST(test_elf_load_ex_dynamic_binary)
{
    pml4_t pml4 = vmm_new_pml4();
    TEST_ASSERT(pml4 != nullptr);

    elf_load_result_t result;
    TEST_ASSERT(elf_load_ex("/bin/echo", &result, pml4));

    /* Must have detected the interpreter */
    TEST_ASSERT(result.interp[0] != '\0');
    TEST_ASSERT(strcmp(result.interp, "/lib/ld.so") == 0);

    /* Interpreter must be loaded at a nonzero base with a valid entry */
    TEST_ASSERT(result.interp_base != 0);
    TEST_ASSERT(result.interp_entry != 0);
    TEST_ASSERT(result.interp_entry > result.interp_base);

    /* Program header info must be populated */
    TEST_ASSERT(result.phdr_vaddr != 0);
    TEST_ASSERT(result.phent == sizeof(Elf64_Phdr));
    TEST_ASSERT(result.phnum > 0);

    /* Executable entry point must be in the expected range */
    TEST_ASSERT(result.entry >= 0x400000);
    TEST_ASSERT(result.max_vaddr > result.entry);

    vmm_destroy_pml4(pml4);
    return true;
}

/**
 * @brief Verify elf_load_ex returns zero interp fields for a static binary.
 */
TEST(test_elf_load_ex_static_binary)
{
    pml4_t pml4 = vmm_new_pml4();
    TEST_ASSERT(pml4 != nullptr);

    elf_load_result_t result;
    TEST_ASSERT(elf_load_ex("/bin/cat", &result, pml4));

    /* Static binary must have no interpreter */
    TEST_ASSERT(result.interp[0] == '\0');
    TEST_ASSERT(result.interp_base == 0);
    TEST_ASSERT(result.interp_entry == 0);

    /* Entry point and max_vaddr must still be valid */
    TEST_ASSERT(result.entry != 0);
    TEST_ASSERT(result.max_vaddr > 0);

    vmm_destroy_pml4(pml4);
    return true;
}

/**
 * @brief Verify elf_load_ex populates phdr fields consistently.
 */
TEST(test_elf_load_ex_phdr_fields)
{
    pml4_t pml4 = vmm_new_pml4();
    TEST_ASSERT(pml4 != nullptr);

    elf_load_result_t result;
    TEST_ASSERT(elf_load_ex("/bin/echo", &result, pml4));

    /* phent must match the standard size */
    TEST_ASSERT(result.phent == sizeof(Elf64_Phdr));

    /* phnum must be reasonable (not zero, not absurdly large) */
    TEST_ASSERT(result.phnum >= 2);
    TEST_ASSERT(result.phnum < 64);

    /* phdr_vaddr must point to readable mapped memory */
    TEST_ASSERT(result.phdr_vaddr != 0);

    vmm_destroy_pml4(pml4);
    return true;
}

/**
 * @brief Verify legacy elf_load still works with dynamic binaries.
 *
 * The old interface should load the executable's PT_LOAD segments
 * but ignore PT_INTERP (since it has no way to report it).
 */
TEST(test_elf_load_legacy_with_dynamic)
{
    pml4_t pml4 = vmm_new_pml4();
    TEST_ASSERT(pml4 != nullptr);

    uint64_t entry = 0, max_vaddr = 0;
    TEST_ASSERT(elf_load("/bin/echo", &entry, &max_vaddr, pml4));
    TEST_ASSERT(entry != 0);
    TEST_ASSERT(max_vaddr > 0);

    vmm_destroy_pml4(pml4);
    return true;
}

/**
 * @brief End-to-end: spawn a dynamically linked test binary and verify exit 0.
 *
 * This exercises the entire dynamic linking pipeline: kernel loads the binary
 * and ld.so, ld.so maps libc.so, resolves symbols, relocates, and transfers
 * control. The test binary then exercises printf, malloc, strcmp, errno, etc.
 */
TEST(test_dynlink_end_to_end)
{
    int pid = sys_spawn("/tests/dynlink_test");
    TEST_ASSERT(pid > 1);

    int status = -1;
    int waited = sys_wait(&status);
    TEST_ASSERT(waited == pid);
    TEST_ASSERT(status == 0);
    return true;
}
