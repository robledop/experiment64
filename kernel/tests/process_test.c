#include "test.h"
#include "process.h"
#include "string.h"
#include "terminal.h"

static void test_thread_entry(void)
{
    printk("Test thread running!\n");
    while (1)
        yield();
}

TEST(test_process_creation)
{
    process_t *proc = process_create("test_proc");
    if (!proc)
    {
        printk("Failed to create process\n");
        return false;
    }

    if (strcmp(proc->name, "test_proc") != 0)
    {
        printk("Process name mismatch: %s\n", proc->name);
        return false;
    }

    if (proc->pid <= 1) // PID 1 is kernel
    {
        printk("Invalid PID: %d\n", proc->pid);
        return false;
    }

    thread_t *thread = thread_create(proc, test_thread_entry, false);
    if (!thread)
    {
        printk("Failed to create thread\n");
        return false;
    }

    if (thread->process != proc)
    {
        printk("Thread process mismatch\n");
        return false;
    }

    if (thread->state != THREAD_READY)
    {
        printk("Thread state mismatch\n");
        return false;
    }

    printk("Process and thread created successfully. PID: %d, TID: %d\n", proc->pid, thread->tid);
    return true;
}

static volatile int thread_ran = 0;

static void scheduler_thread_entry(void)
{
    thread_ran = 1;
    printk("Scheduler thread running!\n");
    // Yield back to main thread
    yield();

    // Exit thread
    printk("Scheduler thread exiting.\n");
}

TEST(test_scheduler)
{
    thread_ran = 0;
    process_t *proc = process_create("sched_test");
    if (!proc)
        return false;

    thread_t *t = thread_create(proc, scheduler_thread_entry, false);
    if (!t)
        return false;

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
        printk("Scheduler test passed: Thread ran.\n");
        return true;
    }
    else
    {
        printk("Scheduler test failed: Thread did not run.\n");
        return false;
    }
}
