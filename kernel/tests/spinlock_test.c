#include "test.h"
#include "spinlock.h"
#include "process.h"

TEST(test_spinlock_basic)
{
    spinlock_t lock;
    spinlock_init(&lock);

    TEST_ASSERT(!lock.locked);

    spinlock_acquire(&lock);
    TEST_ASSERT(lock.locked);

    spinlock_release(&lock);
    TEST_ASSERT(!lock.locked);
    return true;
}

static spinlock_t g_lock;
static volatile int g_counter = 0;
static volatile bool g_thread_done = false;

static void contention_thread(void)
{
    printk("Thread: Starting...\n");
    // Try to acquire lock
    spinlock_acquire(&g_lock);
    printk("Thread: Acquired lock!\n");
    g_counter++;
    spinlock_release(&g_lock);

    g_thread_done = true;
    printk("Thread: Done.\n");

    // Just spin/yield until killed
    while (1)
        yield();
}

TEST(test_spinlock_contention)
{
    spinlock_init(&g_lock);
    g_counter = 0;
    g_thread_done = false;

    printk("Main: Creating process...\n");
    // Create a kernel process/thread for testing
    process_t *proc = process_create("spinlock_test_proc");
    TEST_ASSERT(proc != nullptr);

    printk("Main: Creating thread...\n");
    thread_t *t = thread_create(proc, contention_thread, false);
    TEST_ASSERT(t != nullptr);

    // Acquire lock in main thread
    spinlock_acquire(&g_lock);
    printk("Main: Acquired lock.\n");

    // Yield multiple times to ensure the other thread gets scheduled
    // and tries to acquire the lock (it should spin)
    for (int i = 0; i < 5; i++)
    {
        printk("Main: Yielding %d...\n", i);
        yield();
    }

    // We still hold the lock, so the other thread should not have incremented counter
    TEST_ASSERT(g_counter == 0);

    // Release lock
    spinlock_release(&g_lock);
    printk("Main: Released lock.\n");

    // Yield to let other thread acquire and finish
    int timeout = 100000;
    while (!g_thread_done && timeout > 0)
    {
        if (timeout % 10000 == 0)
            printk("Main: Waiting... %d\n", timeout);
        yield();
        timeout--;
    }

    TEST_ASSERT(g_thread_done);
    TEST_ASSERT(g_counter == 1);
    return true;
}
