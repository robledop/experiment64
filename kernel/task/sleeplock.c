#include "sleeplock.h"
#include "process.h"
#include "cpu.h"

void sleeplock_init(sleeplock_t *lk, const char *name)
{
    spinlock_init(&lk->lock);
    lk->locked = false;
    lk->pid = 0;
    lk->name = name;
}

void sleeplock_acquire(sleeplock_t *lk)
{
    spinlock_acquire(&lk->lock);
    while (lk->locked)
    {
        thread_sleep(lk, &lk->lock);
    }
    lk->locked = true;
    lk->pid = get_current_process() ? get_current_process()->pid : 0;
    spinlock_release(&lk->lock);
}

void sleeplock_release(sleeplock_t *lk)
{
    spinlock_acquire(&lk->lock);
    lk->locked = false;
    lk->pid = 0;
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
