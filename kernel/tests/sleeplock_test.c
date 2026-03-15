#include <tests/test.h>
#include <task/sleeplock.h>
#include <task/process.h>
#include <drivers/terminal.h>

TEST(test_sleeplock_basic)
{
    sleeplock_t lk;
    sleeplock_init(&lk, "test_sl");

    TEST_ASSERT(!sleeplock_holding(&lk));

    sleeplock_acquire(&lk);
    TEST_ASSERT(sleeplock_holding(&lk));

    sleeplock_release(&lk);
    TEST_ASSERT(!sleeplock_holding(&lk));
    return true;
}

static sleeplock_t g_sleeplock;
static volatile int g_sl_counter = 0;
static volatile bool g_sl_done = false;

static void sleeplock_contention_thread(void)
{
    sleeplock_acquire(&g_sleeplock);
    g_sl_counter++;
    sleeplock_release(&g_sleeplock);
    g_sl_done = true;
}

TEST(test_sleeplock_contention)
{
    sleeplock_init(&g_sleeplock, "contention");
    g_sl_counter = 0;
    g_sl_done = false;

    process_t *proc = process_create("sl_test");
    TEST_ASSERT(proc != nullptr);
    thread_t *t = thread_create(proc, sleeplock_contention_thread, false);
    if (!t)
    {
        process_destroy(proc);
        return false;
    }

    sleeplock_acquire(&g_sleeplock);

    // Yield to let the other thread attempt to acquire (it should block).
    for (int i = 0; i < 5; i++)
        yield();

    TEST_ASSERT(g_sl_counter == 0);

    sleeplock_release(&g_sleeplock);

    for (int i = 0; i < 2000 && !g_sl_done; i++)
        yield();

    TEST_ASSERT(g_sl_done);
    TEST_ASSERT(g_sl_counter == 1);

    for (int i = 0; i < 1000; i++)
        yield();
    process_destroy(proc);
    return true;
}
