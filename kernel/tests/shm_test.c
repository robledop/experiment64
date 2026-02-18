#include <tests/test.h>
#include <ipc/shm.h>
#include <mem/vmm.h>
#include <task/process.h>
#include <lib/string.h>
#include <drivers/terminal.h>

TEST(test_shm_create_and_lookup)
{
    shm_init();

    shm_entry_t *entry = shm_create("test_region", 4096);
    TEST_ASSERT(entry != nullptr);
    TEST_ASSERT(entry->size == 4096);
    TEST_ASSERT(entry->num_pages == 1);
    TEST_ASSERT(entry->refcount == 0);
    TEST_ASSERT(strcmp(entry->name, "test_region") == 0);

    shm_entry_t *found = shm_lookup("test_region");
    TEST_ASSERT(found == entry);

    shm_entry_t *not_found = shm_lookup("nonexistent");
    TEST_ASSERT(not_found == nullptr);

    shm_do_unlink("test_region");
    return true;
}

TEST(test_shm_create_multipage)
{
    shm_init();

    shm_entry_t *entry = shm_create("big_region", 8192);
    TEST_ASSERT(entry != nullptr);
    TEST_ASSERT(entry->num_pages == 2);

    shm_entry_t *entry2 = shm_create("odd_size", 5000);
    TEST_ASSERT(entry2 != nullptr);
    TEST_ASSERT(entry2->num_pages == 2);

    shm_do_unlink("big_region");
    shm_do_unlink("odd_size");
    return true;
}

TEST(test_shm_refcount)
{
    shm_init();

    shm_entry_t *entry = shm_create("reftest", 4096);
    TEST_ASSERT(entry != nullptr);
    TEST_ASSERT(entry->refcount == 0);

    shm_ref(entry);
    TEST_ASSERT(entry->refcount == 1);

    shm_ref(entry);
    TEST_ASSERT(entry->refcount == 2);

    shm_unref(entry);
    TEST_ASSERT(entry->refcount == 1);

    TEST_ASSERT(shm_lookup("reftest") != nullptr);

    shm_do_unlink("reftest");
    TEST_ASSERT(shm_lookup("reftest") != nullptr);

    shm_unref(entry);
    TEST_ASSERT(shm_lookup("reftest") == nullptr);

    return true;
}

TEST(test_shm_unlink_no_refs)
{
    shm_init();

    shm_entry_t *entry = shm_create("unlinktest", 4096);
    TEST_ASSERT(entry != nullptr);

    int ret = shm_do_unlink("unlinktest");
    TEST_ASSERT(ret == 0);
    TEST_ASSERT(shm_lookup("unlinktest") == nullptr);

    ret = shm_do_unlink("unlinktest");
    TEST_ASSERT(ret != 0);

    return true;
}

TEST(test_shm_create_invalid)
{
    shm_init();

    TEST_ASSERT(shm_create(nullptr, 4096) == nullptr);
    TEST_ASSERT(shm_create("valid", 0) == nullptr);

    char long_name[SHM_NAME_MAX + 1];
    memset(long_name, 'a', SHM_NAME_MAX);
    long_name[SHM_NAME_MAX] = '\0';
    TEST_ASSERT(shm_create(long_name, 4096) == nullptr);

    return true;
}

TEST(test_shm_pages_zeroed)
{
    shm_init();

    shm_entry_t *entry = shm_create("zerotest", 4096);
    TEST_ASSERT(entry != nullptr);
    TEST_ASSERT(entry->phys_pages != nullptr);
    TEST_ASSERT(entry->phys_pages[0] != 0);

    uint8_t *virt = (uint8_t *)(entry->phys_pages[0] + g_hhdm_offset);
    bool all_zero = true;
    for (int i = 0; i < 4096; i++)
    {
        if (virt[i] != 0)
        {
            all_zero = false;
            break;
        }
    }
    TEST_ASSERT(all_zero);

    shm_do_unlink("zerotest");
    return true;
}
