#include <tests/test.h>
#include <lib/elf.h>
#include <mem/vmm.h>
#include <lib/string.h>
#include <tests/test_util.h>

TEST(test_elf_load_nonexistent_file)
{
    pml4_t pml4 = vmm_new_pml4();
    TEST_ASSERT(pml4 != nullptr);

    elf_load_result_t result;
    TEST_ASSERT(!elf_load("/nonexistent_elf_xyz", &result, pml4));

    vmm_destroy_pml4(pml4);
    return true;
}

TEST(test_elf_load_bad_magic)
{
    const char *path = "/mnt/BAD_ELF.BIN";

    // Write a file that starts with wrong magic bytes.
    uint8_t garbage[64];
    memset(garbage, 0x41, sizeof(garbage));
    TEST_ASSERT(test_vfs_write_file(path, 0, garbage, sizeof(garbage)));

    pml4_t pml4 = vmm_new_pml4();
    TEST_ASSERT(pml4 != nullptr);

    elf_load_result_t result;
    TEST_ASSERT(!elf_load(path, &result, pml4));

    vmm_destroy_pml4(pml4);
    return true;
}

TEST(test_elf_load_truncated_header)
{
    const char *path = "/mnt/TRUNC_ELF.BIN";

    // Write only the first 4 bytes of a valid ELF magic, no actual header.
    uint8_t partial[4] = {0x7F, 'E', 'L', 'F'};
    TEST_ASSERT(test_vfs_write_file(path, 0, partial, sizeof(partial)));

    pml4_t pml4 = vmm_new_pml4();
    TEST_ASSERT(pml4 != nullptr);

    elf_load_result_t result;
    TEST_ASSERT(!elf_load(path, &result, pml4));

    vmm_destroy_pml4(pml4);
    return true;
}

TEST(test_elf_load_valid_binary)
{
    // /bin/init is a known-good ELF in the test image.
    pml4_t pml4 = vmm_new_pml4();
    TEST_ASSERT(pml4 != nullptr);

    elf_load_result_t result;
    TEST_ASSERT(elf_load("/bin/init", &result, pml4));
    TEST_ASSERT(result.entry != 0);
    TEST_ASSERT(result.max_vaddr > result.entry);

    vmm_destroy_pml4(pml4);
    return true;
}
