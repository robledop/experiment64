#include <arch/x86_64/smp.h>
#include <mem/heap.h>
#include <syscall_common.h>

thread_t *find_thread_by_tid(process_t *proc, int tid)
{
    if (!proc || tid <= 0)
        return nullptr;

    thread_t *t;
    list_foreach_entry(t, &proc->threads, list)
    {
        if (t->tid == tid)
            return t;
    }

    return nullptr;
}

bool thread_active_on_any_cpu(thread_t *t)
{
    if (!t)
        return false;

    const uint32_t cpu_count = smp_get_cpu_count();
    for (uint32_t i = 0; i < cpu_count; i++) {
        cpu_t *cpu = smp_get_cpu_by_index(i);
        if (cpu && cpu->active_thread == t)
            return true;
    }

    return false;
}

void free_thread_resources(thread_t *t)
{
    if (!t)
        return;

    if (t->kstack_top != 0) {
        auto kstack_base = (void *)(t->kstack_top - KSTACK_SIZE);
        kfree(kstack_base);
    }

    kfree(t);
}
