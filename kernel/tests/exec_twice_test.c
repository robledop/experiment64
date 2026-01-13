#include <tests/test.h>
#include <lib/elf.h>
#include <mem/vmm.h>
#include <drivers/terminal.h>
#include <drivers/tsc.h>

// Regression: executing a program twice from the same shell/session should not
// corrupt VFS state or crash in path resolution.
TEST(test_exec_twice_elf_load)
{
    // We want to exercise the same VFS/EXT2 path walk done by execve(), without
    // depending on user-mode syscall test stubs.

    const char *path = "/bin/wm";

    for (int i = 0; i < 2; i++)
    {

        pml4_t pml4 = vmm_new_pml4();
        TEST_ASSERT(pml4 != nullptr);

        uint64_t entry = 0;
        uint64_t max_vaddr = 0;


        bool ok = elf_load(path, &entry, &max_vaddr, pml4);



        vmm_destroy_pml4(pml4);


        if (!ok)
        {
            printk("elf_load failed for %s on iteration %d\n", path, i);
            return false;
        }
        TEST_ASSERT(entry != 0);
    }

    return true;
}
