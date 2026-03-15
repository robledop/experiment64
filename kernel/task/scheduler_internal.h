#pragma once

#include <task/process.h>

#define SCHEDULER_MAX_CPUS 32

extern thread_t *scheduler_idle_threads[SCHEDULER_MAX_CPUS];
extern process_t *scheduler_rr_last_proc[SCHEDULER_MAX_CPUS];

[[gnu::used]]
static inline void scheduler_thread_list_move_to_tail(thread_t *t)
{
    if (!t || !t->process)
        return;
    list_del(&t->list);
    list_add_tail(&t->list, &t->process->threads);
}

void scheduler_collect_detached_terminated_threads(list_item_t *free_list);
process_t *scheduler_claim_auto_reap_locked(void);
thread_t *scheduler_find_next_thread_locked(cpu_t *cpu);
bool scheduler_validate_next_thread_locked(thread_t *next, const char *ctx);
void scheduler_release_thread_list(list_item_t *free_list);
