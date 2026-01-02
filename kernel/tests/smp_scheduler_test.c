#include "test.h"
#include <stdint.h>
#include "process.h"
#include "cpu.h"
#include "smp.h"
#include "terminal.h"
#include "apic.h"

// SMP scheduler sanity tests.
// Goal: prove that APs (cpu_index != 0) can actually execute runnable threads.

static volatile uint32_t g_seen_cpu_mask = 0;
static volatile int g_threads_completed = 0;

static void smp_worker_entry(void)
{
    cpu_t* cpu = get_cpu();
    if (cpu)
    {
        const uint32_t idx = (cpu->cpu_index >= 0 && cpu->cpu_index < 32) ? (uint32_t)cpu->cpu_index : 0;
        __atomic_fetch_or(&g_seen_cpu_mask, (1u << idx), __ATOMIC_SEQ_CST);
    }

    __atomic_fetch_add(&g_threads_completed, 1, __ATOMIC_SEQ_CST);

    // Return immediately; keeping this short reduces test flakiness.
}

TEST_PRIO(test_smp_aps_execute_threads, 5)
{
    const uint32_t cpu_count = smp_get_cpu_count();

    // If only BSP present, nothing to prove.
    if (cpu_count <= 1)
        return true;

    g_seen_cpu_mask = 0;
    g_threads_completed = 0;

    // Create a bunch of short-lived threads to increase scheduling opportunities.
    // We intentionally create more threads than CPUs.
    const int num_threads = (int)(cpu_count * 4);

    process_t* p = process_create("smp_sched");
    TEST_ASSERT(p != nullptr);

    for (int i = 0; i < num_threads; i++)
    {
        thread_t* t = thread_create(p, smp_worker_entry, false);
        TEST_ASSERT(t != nullptr);
    }

    // Wake APs and encourage immediate load balancing.
    apic_send_ipi_all_excluding_self(IPI_RESCHEDULE_VECTOR);

    // Wait for all threads to complete.
    // Bound by iterations so tests don't hang.
    for (int i = 0; i < 200000; i++)
    {
        if (__atomic_load_n(&g_threads_completed, __ATOMIC_SEQ_CST) >= num_threads)
            break;
        yield();
    }

    const int done = __atomic_load_n(&g_threads_completed, __ATOMIC_SEQ_CST);
    if (done < num_threads)
    {
        printk("SMP sched: only %d/%d threads completed\n", done, num_threads);
        return false;
    }

    const uint32_t mask = __atomic_load_n(&g_seen_cpu_mask, __ATOMIC_SEQ_CST);

    // Require that at least one AP ran at least one thread.
    // (Mask bit 0 is BSP; any other bit implies AP.)
    const bool ap_ran = (mask & ~1u) != 0;
    if (!ap_ran)
    {
        printk("SMP sched: threads only observed on BSP (mask=0x%x, cpu_count=%u)\n", mask, cpu_count);
        return false;
    }

    return true;
}
