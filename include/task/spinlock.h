#pragma once

#include <arch/x86_64/cpu.h>

typedef struct {
    volatile bool locked;
} spinlock_t;

void spinlock_init(spinlock_t *lock);
void spinlock_acquire(spinlock_t *lock);
void spinlock_release(spinlock_t *lock);
void spinlock_assert_held(const spinlock_t *lock);

// Interrupt-safe spinlock macros
// Usage:
//   uint64_t flags;
//   SPIN_LOCK_INT_SAVE(lock, flags);
//   ... critical section ...
//   SPIN_UNLOCK_INT_RESTORE(lock, flags);
#define SPIN_LOCK_INT_SAVE(lock, flags)                                                                                \
    do {                                                                                                               \
        __asm__ volatile("pushfq; pop %0; cli" : "=r"(flags)::"memory");                                               \
        spinlock_acquire(&(lock));                                                                                     \
    } while (0)

#define SPIN_UNLOCK_INT_RESTORE(lock, flags)                                                                           \
    do {                                                                                                               \
        spinlock_release(&(lock));                                                                                     \
        if ((flags) & RFLAGS_IF)                                                                                       \
            __asm__ volatile("sti" ::: "memory");                                                                      \
    } while (0)

/**
 * @brief RAII-style scope guard for interrupt-safe spinlocks.
 *
 * Implementation detail — callers should use the WITH_LOCK() macro below
 * instead of manipulating this struct directly.
 */
typedef struct {
    spinlock_t *lock;
    uint64_t rflags;
    bool once;
} autolock_t;

__attribute__((used)) static inline autolock_t _autolock_acquire(spinlock_t *lock)
{
    autolock_t a = {.lock = lock};
    __asm__ volatile("pushfq; pop %0; cli" : "=r"(a.rflags)::"memory");
    spinlock_acquire(lock);
    a.once = true;
    return a;
}

__attribute__((used)) static inline void _autolock_release(autolock_t *a)
{
    spinlock_release(a->lock);
    if (a->rflags & RFLAGS_IF)
        __asm__ volatile("sti" ::: "memory");
}

/**
 * @brief Run a block with an interrupt-safe spinlock held.
 *
 * Acquires @p lock_var (disabling interrupts if they were enabled) for the
 * lexical scope of the following statement or block, and releases it
 * automatically on scope exit — including early `return`, `break`, or
 * `continue`. Restores the caller's IF state as it was before acquire.
 *
 * Prefer this over manual SPIN_LOCK_INT_SAVE / SPIN_UNLOCK_INT_RESTORE pairs
 * for short critical sections with multiple exit paths.
 *
 * Usage:
 *   WITH_LOCK(some_lock) {
 *       // critical section; may return early
 *   }
 *
 * @warning The body must not release the underlying lock manually. Calls
 *          that atomically release-and-reacquire (e.g. thread_sleep()) are
 *          safe because the lock is held again by the time the body returns.
 */
#define WITH_LOCK(lock_var)                                                                                            \
    for (autolock_t _al __attribute__((cleanup(_autolock_release))) = _autolock_acquire(&(lock_var)); _al.once;        \
         _al.once                                                   = false)
