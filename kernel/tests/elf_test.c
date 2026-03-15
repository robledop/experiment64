#include <tests/test.h>
#include <lib/elf.h>
#include <mem/vmm.h>
#include <lib/string.h>
#include <tests/test_util.h>

TEST(test_elf_load_nonexistent_file)
{
    pml4_t pml4 = vmm_new_pml4();
    TEST_ASSERT(pml4 != nullptr);

    uint64_t entry = 0, max_vaddr = 0;
    TEST_ASSERT(!elf_load("/nonexistent_elf_xyz", &entry, &max_vaddr, pml4));

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

    uint64_t entry = 0, max_vaddr = 0;
    TEST_ASSERT(!elf_load(path, &entry, &max_vaddr, pml4));

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

    uint64_t entry = 0, max_vaddr = 0;
    TEST_ASSERT(!elf_load(path, &entry, &max_vaddr, pml4));

    vmm_destroy_pml4(pml4);
    return true;
}

TEST(test_elf_load_valid_binary)
{
    // /bin/init is a known-good ELF in the test image.
    pml4_t pml4 = vmm_new_pml4();
    TEST_ASSERT(pml4 != nullptr);

    uint64_t entry = 0, max_vaddr = 0;
    TEST_ASSERT(elf_load("/bin/init", &entry, &max_vaddr, pml4));
    TEST_ASSERT(entry != 0);
    TEST_ASSERT(max_vaddr > entry);

    vmm_destroy_pml4(pml4);
    return true;
}
