#include <tests/test.h>
#include <lib/elf.h>
#include <mem/vmm.h>
#include <drivers/terminal.h>

// Regression: executing a program twice from the same shell/session should not
// corrupt the VFS state or crash in path resolution.
TEST(test_exec_twice_elf_load)
{
    // We want to exercise the same VFS/EXT2 path walk done by execve(), without
    // depending on user-mode syscall test stubs.

    const char* path = "/bin/wm";

    for (int i = 0; i < 2; i++)
    {
        pml4_t pml4 = vmm_new_pml4();
        TEST_ASSERT(pml4 != nullptr);

        elf_load_result_t result;
        bool ok = elf_load(path, &result, pml4);

        vmm_destroy_pml4(pml4);

        if (!ok)
        {
            printk("elf_load failed for %s on iteration %d\n", path, i);
            return false;
        }
        TEST_ASSERT(result.entry != 0);
    }

    return true;
}
