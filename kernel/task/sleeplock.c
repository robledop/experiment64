#include <task/sleeplock.h>
#include <task/process.h>
#include <debug.h>

static void sleeplock_check_context(void)
{
    if (!scheduler_is_ready())
        panic("sleeplock: scheduler not ready");
    if (cpu_in_interrupt())
        panic("sleeplock: interrupt context");
}

void sleeplock_init(sleeplock_t *lk, const char *name)
{
    spinlock_init(&lk->lock);
    lk->locked = false;
    lk->pid    = 0;
    lk->name   = name;
}

void sleeplock_acquire(sleeplock_t *lk)
{
    sleeplock_check_context();
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
    sleeplock_check_context();
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
    bool r = lk->locked && (get_current_process() ? (lk->pid == get_current_process()->pid) : false);
    spinlock_release(&lk->lock);
    return r;
}

void sleeplock_assert_held(sleeplock_t *lk)
{
    if (!lk || !sleeplock_holding(lk)) {
        panic("sleeplock not held");
    }
}
