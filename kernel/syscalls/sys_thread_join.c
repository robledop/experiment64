#include <sys/syscall.h>
#include <syscall_common.h>
#include <arch/x86_64/smp.h>
#include <arch/x86_64/cpu.h>
#include <mem/heap.h>

static thread_t* find_thread_by_tid(process_t* proc, int tid)
{
    if (!proc || tid <= 0)
        return nullptr;

    thread_t* t;
    list_foreach_entry(t, &proc->threads, list)
    {
        if (t->tid == tid) return t;
    }

    return nullptr;
}

static bool thread_active_on_any_cpu(thread_t* t)
{
    if (!t) return false;

    const uint32_t cpu_count = smp_get_cpu_count();
    for (uint32_t i = 0; i < cpu_count; i++)
    {
        cpu_t* cpu = smp_get_cpu_by_index(i);
        if (cpu && cpu->active_thread == t)
            return true;
    }

    return false;
}

int sys_thread_join(int tid, int* status)
{
    if (!current_thread || !current_thread->is_user || !current_process)
        return -1;

    if (tid <= 0) return -1;
    if (status && !user_ptr_write_ok(status, sizeof(*status), "sys_thread_join status"))
        return -1;

    for (;;)
    {
        uint64_t rflags;
        SPIN_LOCK_INT_SAVE(scheduler_lock, rflags);

        thread_t* target = find_thread_by_tid(current_process, tid);
        if (!target || !target->is_user)
        {
            SPIN_UNLOCK_INT_RESTORE(scheduler_lock, rflags);
            return -1;
        }

        if (target == current_thread)
        {
            SPIN_UNLOCK_INT_RESTORE(scheduler_lock, rflags);
            return -1;
        }

        if (target->state != THREAD_TERMINATED)
        {
            thread_sleep(target, &scheduler_lock);
            SPIN_UNLOCK_INT_RESTORE(scheduler_lock, rflags);
            continue;
        }

        if (thread_active_on_any_cpu(target))
        {
            SPIN_UNLOCK_INT_RESTORE(scheduler_lock, rflags);
            yield();
            continue;
        }

        const int exit_code = target->exit_code;

        list_del(&target->list);
        target->process = nullptr;
        SPIN_UNLOCK_INT_RESTORE(scheduler_lock, rflags);

        if (status)
        {
            if (!copy_to_user(status, &exit_code, sizeof(exit_code)))
            {
                if (target->kstack_top != 0)
                {
                    void* kstack_base = (void*)(target->kstack_top - KSTACK_SIZE);
                    kfree(kstack_base);
                }
                kfree(target);
                return -1;
            }
        }

        if (target->kstack_top != 0)
        {
            void* kstack_base = (void*)(target->kstack_top - KSTACK_SIZE);
            kfree(kstack_base);
        }
        kfree(target);
        return 0;
    }
}
