#include "test.h"
#include "process.h"
#include "string.h"
#include "terminal.h"

static void test_thread_entry(void)
{
    printf("Test thread running!\n");
    while (1)
        yield();
}

TEST(test_process_creation)
{
    process_t *proc = process_create("test_proc");
    if (!proc)
    {
        printf("Failed to create process\n");
        return false;
    }

    if (strcmp(proc->name, "test_proc") != 0)
    {
        printf("Process name mismatch: %s\n", proc->name);
        return false;
    }

    if (proc->pid <= 1) // PID 1 is kernel
    {
        printf("Invalid PID: %d\n", proc->pid);
        return false;
    }

    thread_t *thread = thread_create(proc, test_thread_entry, false);
    if (!thread)
    {
        printf("Failed to create thread\n");
        return false;
    }

    if (thread->process != proc)
    {
        printf("Thread process mismatch\n");
        return false;
    }

    if (thread->state != THREAD_READY)
    {
        printf("Thread state mismatch\n");
        return false;
    }

    printf("Process and thread created successfully. PID: %d, TID: %d\n", proc->pid, thread->tid);
    return true;
}

static volatile int thread_ran = 0;

static void scheduler_thread_entry(void)
{
    printf("Scheduler thread running!\n");
    thread_ran = 1;
    // Yield back to main thread
    yield();

    // Exit thread
    printf("Scheduler thread exiting.\n");
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

    printf("Yielding to scheduler thread...\n");
    yield(); // Should switch to t

    if (thread_ran)
    {
        printf("Scheduler test passed: Thread ran.\n");
        return true;
    }
    else
    {
        printf("Scheduler test failed: Thread did not run.\n");
        return false;
    }
}
