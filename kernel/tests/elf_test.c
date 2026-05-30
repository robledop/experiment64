#include <tests/test.h>
#include <lib/elf.h>
#include <mem/vmm.h>
#include <lib/string.h>
#include <tests/test_util.h>

TEST(test_elf_segment_user_range)
{
    // A normal user binary segment (loads near 0x400000) must be accepted.
    TEST_ASSERT(elf_segment_in_user_space(0x400000, 0x1000));
    // The last page of the user half is still valid.
    TEST_ASSERT(elf_segment_in_user_space(0x00007FFFFFFFF000ULL, 0x1000));

    // A p_vaddr in the kernel's higher half must be rejected — mapping it would
    // corrupt the shared kernel page tables.
    TEST_ASSERT(!elf_segment_in_user_space(0xFFFFFFFF80000000ULL, 0x1000));
    // The very first kernel-half address (PML4 entry 256) must be rejected too.
    TEST_ASSERT(!elf_segment_in_user_space(0x0000800000000000ULL, 0x1000));
    // A segment that starts in the user half but spills across the boundary.
    TEST_ASSERT(!elf_segment_in_user_space(0x00007FFFFFFFF000ULL, 0x2000));
    // A size so large the address arithmetic overflows must be rejected.
    TEST_ASSERT(!elf_segment_in_user_space(0xFFFFFFFFFFFFF000ULL, 0x4000));
    return true;
}

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
