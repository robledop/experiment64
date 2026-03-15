#include <tests/test.h>
#include <task/process.h>
#include <lib/list.h>

TEST(test_vm_area_add_and_lookup)
{
    process_t *proc = process_create("vma_test");
    TEST_ASSERT(proc != nullptr);

    vm_area_t *vma = vm_area_add(proc, 0x1000, 0x2000, VMA_READ | VMA_WRITE);
    TEST_ASSERT(vma != nullptr);
    TEST_ASSERT(vma->start == 0x1000);
    TEST_ASSERT(vma->end == 0x2000);
    TEST_ASSERT(vma->flags == (VMA_READ | VMA_WRITE));

    vm_area_t *vma2 = vm_area_add(proc, 0x3000, 0x4000, VMA_READ | VMA_EXEC);
    TEST_ASSERT(vma2 != nullptr);
    TEST_ASSERT(vma2->start == 0x3000);

    // Verify both are in the list
    int count = 0;
    vm_area_t *pos;
    list_foreach_entry(pos, &proc->vm_areas, list)
    {
        count++;
    }
    TEST_ASSERT(count == 2);

    process_destroy(proc);
    return true;
}

TEST(test_vm_area_clear)
{
    process_t *proc = process_create("vma_clear");
    TEST_ASSERT(proc != nullptr);

    vm_area_add(proc, 0x1000, 0x2000, VMA_READ);
    vm_area_add(proc, 0x3000, 0x4000, VMA_WRITE);

    TEST_ASSERT(!list_empty(&proc->vm_areas));

    vm_area_clear(proc);

    TEST_ASSERT(list_empty(&proc->vm_areas));

    process_destroy(proc);
    return true;
}

TEST(test_vm_area_clone)
{
    process_t *src = process_create("vma_src");
    process_t *dst = process_create("vma_dst");
    TEST_ASSERT(src != nullptr && dst != nullptr);

    vm_area_add(src, 0x1000, 0x2000, VMA_READ | VMA_WRITE);
    vm_area_add(src, 0x5000, 0x6000, VMA_READ | VMA_EXEC);

    vm_area_clone(dst, src);

    // Count entries in dst
    int count = 0;
    vm_area_t *pos;
    list_foreach_entry(pos, &dst->vm_areas, list)
    {
        count++;
    }
    TEST_ASSERT(count == 2);

    // Verify values match
    vm_area_t *first = list_first_entry(&dst->vm_areas, vm_area_t, list);
    TEST_ASSERT(first->start == 0x1000);
    TEST_ASSERT(first->flags == (VMA_READ | VMA_WRITE));

    process_destroy(src);
    process_destroy(dst);
    return true;
}
