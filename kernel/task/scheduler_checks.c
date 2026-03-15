#include "scheduler_internal.h"

#include <arch/x86_64/smp.h>
#include <debug.h>
#include <drivers/terminal.h>
#include <mem/heap.h>

static bool thread_is_active_on_any_cpu(thread_t *t)
{
    spinlock_assert_held(&scheduler_lock);
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

void scheduler_collect_detached_terminated_threads(list_item_t *free_list)
{
    spinlock_assert_held(&scheduler_lock);
    process_t *process;
    list_foreach_entry(process, &process_list, list) {
        thread_t *thread;
        thread_t *next;
        list_foreach_entry_safe(thread, next, &process->threads, list) {
            if (!thread)
                continue;
            if (!thread->is_user || !thread->detached)
                continue;

            uint32_t raw_state = thread_state_load_raw(thread);
            if (!thread_state_valid_raw(raw_state)) {
                thread_state_store(thread, THREAD_TERMINATED);
                raw_state = THREAD_TERMINATED;
            }
            if ((thread_state_t)raw_state != THREAD_TERMINATED)
                continue;
            if (thread_is_active_on_any_cpu(thread))
                continue;

            list_del(&thread->list);
            thread->process = nullptr;
            list_add_tail(&thread->list, free_list);
        }
    }
}

static bool thread_is_ready(thread_t *thread, bool allow_user, const char *ctx)
{
    spinlock_assert_held(&scheduler_lock);
    uint32_t raw_state = thread_state_load_raw(thread);
    if (!thread_state_valid_raw(raw_state)) {
        boot_message(ERROR,
                     "%s: invalid thread state pid=%d tid=%d state=%u",
                     ctx,
                     thread->process ? thread->process->pid : -1,
                     thread->tid,
                     raw_state);
        thread_state_store(thread, THREAD_TERMINATED);
        return false;
    }

    process_t *process = thread->process;
    if (!process || !process_in_list(process)) {
        boot_message(ERROR,
                     "%s: thread with stale process pid=%d tid=%d",
                     ctx,
                     process ? process->pid : -1,
                     thread->tid);
        thread_state_store(thread, THREAD_TERMINATED);
        return false;
    }

    auto state         = (thread_state_t)raw_state;
    const bool userish = thread->is_user || (thread->saved_user_rsp != 0);
    return state == THREAD_READY && !thread->is_idle && (allow_user || !userish);
}

bool scheduler_tick(void)
{
    if (!scheduler_is_ready())
        return false;

    scheduler_ticks++;
    bool need_resched = false;

    spinlock_acquire(&scheduler_lock);
    process_t *process;
    list_foreach_entry(process, &process_list, list) {
        if (list_empty(&process->threads))
            continue;

        thread_t *thread;
        list_foreach_entry(thread, &process->threads, list) {
            uint32_t raw_state = thread_state_load_raw(thread);
            if (!thread_state_valid_raw(raw_state)) {
                boot_message(ERROR,
                             "scheduler_tick: invalid thread state pid=%d tid=%d state=%u",
                             process->pid,
                             thread->tid,
                             raw_state);
                thread_state_store(thread, THREAD_TERMINATED);
            }
        }
    }

    cpu_t *cpu     = get_cpu();
    thread_t *curr = cpu ? cpu->active_thread : nullptr;
    if (curr) {
        if (cpu && cpu->scheduler_thread == curr) {
            spinlock_release(&scheduler_lock);
            return need_resched;
        }

        const uintptr_t curr_addr = (uintptr_t)curr;
        const bool curr_aligned   = (curr_addr % __alignof__(thread_t)) == 0;

        uint32_t raw_state = curr_aligned ? thread_state_load_raw(curr) : THREAD_TERMINATED;
        bool curr_valid    = curr_aligned && thread_state_valid_raw(raw_state);
        if (!curr_valid) {
            const uintptr_t proc_addr = curr_aligned ? (uintptr_t)curr->process : 0;
            int pid                   = -1;
            int tid                   = -1;
            if (curr_aligned) {
                tid = curr->tid;
                if (curr->process && (proc_addr % __alignof__(process_t) == 0))
                    pid = curr->process->pid;
            }

            boot_message(ERROR,
                         "scheduler_tick: invalid current thread state raw=%u curr=0x%lx pid=%d tid=%d proc=0x%lx",
                         raw_state,
                         (unsigned long)curr_addr,
                         pid,
                         tid,
                         (unsigned long)proc_addr);

            raw_state = THREAD_READY;
            if (curr_aligned)
                thread_state_store(curr, THREAD_READY);
            need_resched = true;
            curr         = nullptr;
        }

        if (curr) {
            auto curr_state = (thread_state_t)raw_state;
            if (!curr->is_idle && curr_state == THREAD_RUNNING) {
                if (curr->ticks_remaining > 0)
                    curr->ticks_remaining--;

                if (curr->ticks_remaining == 0) {
                    thread_state_store(curr, THREAD_READY);
                    scheduler_thread_list_move_to_tail(curr);
                    need_resched = true;
                }
            } else {
                need_resched = true;
            }
        }
    }

    spinlock_release(&scheduler_lock);
    return need_resched;
}

bool process_can_reap_locked(process_t *proc)
{
    spinlock_assert_held(&scheduler_lock);
    if (!proc)
        return false;

    thread_t *thread;
    list_foreach_entry(thread, &proc->threads, list) {
        if (thread_is_active_on_any_cpu(thread))
            return false;

        uint32_t raw_state = thread_state_load_raw(thread);
        if (!thread_state_valid_raw(raw_state)) {
            thread_state_store(thread, THREAD_TERMINATED);
            return false;
        }
        if ((thread_state_t)raw_state != THREAD_TERMINATED)
            return false;
    }

    return true;
}

process_t *scheduler_claim_auto_reap_locked(void)
{
    spinlock_assert_held(&scheduler_lock);
    process_t *process;
    list_foreach_entry(process, &process_list, list) {
        if (!process || !process->terminated || !process->auto_reap || process->auto_reap_claimed)
            continue;
        if (!process_can_reap_locked(process))
            continue;
        process->auto_reap_claimed = true;
        return process;
    }
    return nullptr;
}

static thread_t *find_any_runnable_thread(bool allow_user)
{
    spinlock_assert_held(&scheduler_lock);
    process_t *process;
    list_foreach_entry(process, &process_list, list) {
        thread_t *thread;
        list_foreach_entry(thread, &process->threads, list) {
            if (thread_is_ready(thread, allow_user, "sched") && !thread_is_active_on_any_cpu(thread))
                return thread;
        }
    }
    return nullptr;
}

static thread_t *find_any_runnable_thread_rr(cpu_t *cpu, bool allow_user)
{
    spinlock_assert_held(&scheduler_lock);
    if (!cpu)
        return find_any_runnable_thread(allow_user);

    const int cpu_idx = cpu->cpu_index;
    if (cpu_idx < 0 || cpu_idx >= (int)SCHEDULER_MAX_CPUS)
        return find_any_runnable_thread(allow_user);

    list_item_t *head  = &process_list;
    process_t *startp  = scheduler_rr_last_proc[cpu_idx];
    list_item_t *start = (startp && process_in_list(startp)) ? startp->list.next : head->next;
    if (start == head)
        start = head->next;

    for (list_item_t *pos = start; pos != head; pos = pos->next) {
        process_t *process = list_entry(pos, process_t, list);
        thread_t *thread;
        list_foreach_entry(thread, &process->threads, list) {
            if (thread_is_ready(thread, allow_user, "sched") && !thread_is_active_on_any_cpu(thread)) {
                scheduler_rr_last_proc[cpu_idx] = process;
                return thread;
            }
        }
    }

    for (list_item_t *pos = head->next; pos != start && pos != head; pos = pos->next) {
        process_t *process = list_entry(pos, process_t, list);
        thread_t *thread;
        list_foreach_entry(thread, &process->threads, list) {
            if (thread_is_ready(thread, allow_user, "sched") && !thread_is_active_on_any_cpu(thread)) {
                scheduler_rr_last_proc[cpu_idx] = process;
                return thread;
            }
        }
    }

    return nullptr;
}

thread_t *scheduler_find_next_thread_locked(cpu_t *cpu)
{
    spinlock_assert_held(&scheduler_lock);

    thread_t *next = find_any_runnable_thread_rr(cpu, true);
    if (next)
        return next;

    thread_t *idle    = nullptr;
    const int cpu_idx = cpu ? cpu->cpu_index : -1;
    if (cpu_idx >= 0 && cpu_idx < (int)SCHEDULER_MAX_CPUS)
        idle = scheduler_idle_threads[cpu_idx];
    if (!idle)
        idle = scheduler_idle_threads[0];
    return idle;
}

bool scheduler_validate_next_thread_locked(thread_t *next, const char *ctx)
{
    spinlock_assert_held(&scheduler_lock);
    if (!next)
        return false;
    if (!ctx)
        ctx = "scheduler";

    const uintptr_t ktop  = next->kstack_top;
    const uintptr_t kbase = (ktop != 0) ? (ktop - KSTACK_SIZE) : 0;
    if (ktop == 0 || next->rsp < kbase || next->rsp >= ktop) {
        boot_message(ERROR,
                     "%s: invalid rsp pid=%d tid=%d rsp=0x%lx kstack=[0x%lx-0x%lx)",
                     ctx,
                     next->process ? next->process->pid : -1,
                     next->tid,
                     next->rsp,
                     kbase,
                     ktop);
        thread_state_store(next, THREAD_TERMINATED);
        return false;
    }

    const uintptr_t rip_slot = next->rsp + (6 * sizeof(uint64_t));
    if (rip_slot < kbase || rip_slot + sizeof(uint64_t) > ktop) {
        boot_message(ERROR,
                     "%s: invalid rip slot pid=%d tid=%d rsp=0x%lx kstack=[0x%lx-0x%lx)",
                     ctx,
                     next->process ? next->process->pid : -1,
                     next->tid,
                     next->rsp,
                     kbase,
                     ktop);
        thread_state_store(next, THREAD_TERMINATED);
        return false;
    }

    const uint64_t saved_rip  = *(const uint64_t *)rip_slot;
    const uintptr_t user_top  = g_hhdm_offset ? g_hhdm_offset : 0x0000800000000000ull;
    const bool rip_in_kernel  = saved_rip >= user_top;
    const bool rip_is_nonzero = saved_rip != 0;
    if (!rip_is_nonzero || !rip_in_kernel) {
        boot_message(ERROR,
                     "%s: bad rip pid=%d tid=%d rip=0x%lx rsp=0x%lx",
                     ctx,
                     next->process ? next->process->pid : -1,
                     next->tid,
                     saved_rip,
                     next->rsp);
        thread_state_store(next, THREAD_TERMINATED);
        return false;
    }

    return true;
}

void scheduler_release_thread_list(list_item_t *free_list)
{
    thread_t *thread;
    thread_t *next;
    list_foreach_entry_safe(thread, next, free_list, list) {
        if (!thread)
            continue;

        list_del(&thread->list);
        if (thread->kstack_top != 0) {
            auto kstack_base = (void *)(thread->kstack_top - KSTACK_SIZE);
            kfree(kstack_base);
        }
        kfree(thread);
    }
}
