#include <task/sleeplock.h>
#include <task/process.h>
#include <drivers/terminal.h>
#include <debug.h>

#if defined(DEBUG) || defined(TEST_MODE)
static uint32_t sleeplock_guard_hits = 0;

static void sleeplock_log_misuse(const sleeplock_t *lk, const char *reason, const uint64_t rflags)
{
    const uint32_t hits   = __atomic_add_fetch(&sleeplock_guard_hits, 1, __ATOMIC_RELAXED);
    const process_t *proc = current_process;
    const thread_t *t     = current_thread;
    printk("sleeplock misuse: %s lock=%s pid=%d tid=%d rflags=0x%lx intr=%d hits=%u\n",
           reason,
           (lk != nullptr && lk->name) ? lk->name : "?",
           proc != nullptr ? proc->pid : -1,
           t != nullptr ? t->tid : -1,
           rflags,
           cpu_in_interrupt() ? 1 : 0,
           hits);
}

static void sleeplock_check_context(const sleeplock_t *lk)
{
    uint64_t rflags;
    __asm__ volatile("pushfq; pop %0" : "=r"(rflags));
    if (!scheduler_is_ready()) {
        sleeplock_log_misuse(lk, "scheduler not ready", rflags);
        panic("sleeplock: scheduler not ready");
    }
    if (cpu_in_interrupt()) {
        sleeplock_log_misuse(lk, "interrupt context", rflags);
        panic("sleeplock: interrupt context");
    }
    if ((rflags & RFLAGS_IF) == 0) {
        sleeplock_log_misuse(lk, "interrupts disabled", rflags);
        panic("sleeplock: interrupts disabled");
    }
}
#else
static inline void sleeplock_check_context([[maybe_unused]] const sleeplock_t *lk)
{
}
#endif

void sleeplock_init(sleeplock_t *lk, const char *name)
{
    spinlock_init(&lk->lock);
    lk->locked = false;
    lk->pid    = 0;
    lk->name   = name;
}

void sleeplock_acquire(sleeplock_t *lk)
{
    sleeplock_check_context(lk);
    spinlock_acquire(&lk->lock);
    while (lk->locked) {
        thread_sleep(lk, &lk->lock);
    }
    lk->locked = true;
    lk->pid    = current_process ? current_process->pid : 0;
    lk->tid    = current_thread ? current_thread->tid : 0;
    spinlock_release(&lk->lock);
}

void sleeplock_release(sleeplock_t *lk)
{
    sleeplock_check_context(lk);
    spinlock_acquire(&lk->lock);
    lk->locked = false;
    lk->pid    = 0;
    lk->tid    = 0;
    thread_wakeup(lk);
    spinlock_release(&lk->lock);
}

bool sleeplock_holding(sleeplock_t *lk)
{
    spinlock_acquire(&lk->lock);
    bool r = lk->locked
        && (get_current_process() ? (lk->pid == get_current_process()->pid) : false)
        && (current_thread ? (lk->tid == current_thread->tid) : false);
    spinlock_release(&lk->lock);
    return r;
}

void sleeplock_assert_held(sleeplock_t *lk)
{
    if (!lk || !sleeplock_holding(lk)) {
        panic("sleeplock not held");
    }
}
