#include <tests/test.h>
#include <task/process.h>
#include <lib/string.h>
#include <drivers/terminal.h>
#include <task/spinlock.h>

static volatile bool process_thread_done = false;

static void test_thread_entry(void)
{
    printk("Test thread running!\n");
    process_thread_done = true;
}

TEST(test_process_creation)
{
    process_thread_done = false;
    process_t *proc = process_create("test_proc");
    if (!proc)
    {
        printk("Failed to create process\n");
        return false;
    }

    if (strcmp(proc->name, "test_proc") != 0)
    {
        printk("Process name mismatch: %s\n", proc->name);
        process_destroy(proc);
        return false;
    }

    if (proc->pid <= 1) // PID 1 is kernel
    {
        printk("Invalid PID: %d\n", proc->pid);
        process_destroy(proc);
        return false;
    }

    thread_t *thread = thread_create(proc, test_thread_entry, false);
    if (!thread)
    {
        printk("Failed to create thread\n");
        process_destroy(proc);
        return false;
    }

    if (thread->process != proc)
    {
        printk("Thread process mismatch\n");
        process_destroy(proc);
        return false;
    }

    if (thread->state != THREAD_READY && thread->state != THREAD_RUNNING && thread->state != THREAD_TERMINATED)
    {
        printk("Thread state mismatch\n");
        process_destroy(proc);
        return false;
    }

    for (int i = 0; i < 1000; i++)
    {
        if (process_thread_done)
            break;
        yield();
    }

    if (!process_thread_done)
    {
        printk("Process test thread did not run\n");
    }

    process_destroy(proc);
    printk("Process and thread created successfully. PID: %d, TID: %d\n", proc->pid, thread->tid);
    return true;
}

static volatile int thread_ran = 0;

static volatile bool scheduler_thread_done = false;

static void scheduler_thread_entry(void)
{
    thread_ran = 1;
    printk("Scheduler thread running!\n");
    // Yield back to the main thread
    yield();

    printk("Scheduler thread exiting.\n");
    scheduler_thread_done = true;
}

TEST(test_scheduler)
{
    thread_ran = 0;
    scheduler_thread_done = false;
    process_t *proc = process_create("sched_test");
    if (!proc)
        return false;

    thread_t *t = thread_create(proc, scheduler_thread_entry, false);
    if (!t)
    {
        process_destroy(proc);
        return false;
    }


    printk("Yielding to scheduler thread...\n");

    // Yield a few times to give the thread a chance to run
    for (int i = 0; i < 5; i++)
    {
        yield();
        if (thread_ran)
            break;
    }

    if (thread_ran)
    {
        for (int i = 0; i < 1000; i++)
        {
            if (scheduler_thread_done)
                break;
            yield();
        }
        process_destroy(proc);
        printk("Scheduler test passed: Thread ran.\n");
        return true;
    }
    else
    {
        process_destroy(proc);
        printk("Scheduler test failed: Thread did not run.\n");
        return false;
    }
}

static spinlock_t sleep_lock;
static volatile bool sleep_ready = false;
static volatile bool sleep_woke = false;
static int sleep_channel = 0;

static void scheduler_sleep_entry(void)
{
    spinlock_acquire(&sleep_lock);
    sleep_ready = true;
    thread_sleep(&sleep_channel, &sleep_lock);
    sleep_woke = true;
    spinlock_release(&sleep_lock);
}

TEST(test_scheduler_sleep_wakeup)
{
    spinlock_init(&sleep_lock);
    sleep_ready = false;
    sleep_woke = false;

    process_t *proc = process_create("sleep_wakeup");
    if (!proc)
        return false;

    thread_t *t = thread_create(proc, scheduler_sleep_entry, false);
    if (!t)
    {
        process_destroy(proc);
        return false;
    }

    for (int i = 0; i < 2000; i++)
    {
        if (sleep_ready)
            break;
        yield();
    }

    if (!sleep_ready)
    {
        printk("Sleep test failed: thread did not enter sleep path\n");
        process_destroy(proc);
        return false;
    }

    bool blocked = false;
    for (int i = 0; i < 2000; i++)
    {
        uint64_t flags;
        SPIN_LOCK_INT_SAVE(scheduler_lock, flags);
        thread_state_t state = t->state;
        SPIN_UNLOCK_INT_RESTORE(scheduler_lock, flags);

        if (state == THREAD_BLOCKED)
        {
            blocked = true;
            break;
        }
        yield();
    }

    if (!blocked)
    {
        printk("Sleep test failed: thread did not block\n");
        process_destroy(proc);
        return false;
    }

    thread_wakeup(&sleep_channel);

    for (int i = 0; i < 2000; i++)
    {
        if (sleep_woke)
            break;
        yield();
    }

    if (!sleep_woke)
    {
        printk("Sleep test failed: thread did not wake\n");
        process_destroy(proc);
        return false;
    }

    process_destroy(proc);
    return true;
}
