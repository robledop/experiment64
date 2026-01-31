#pragma once

#include <arch/x86_64/cpu.h>

typedef struct
{
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
#define SPIN_LOCK_INT_SAVE(lock, flags)                                   \
    do                                                                   \
    {                                                                    \
        __asm__ volatile("pushfq; pop %0; cli" : "=r"(flags)::"memory"); \
        spinlock_acquire(&(lock));                                       \
    } while (0)

#define SPIN_UNLOCK_INT_RESTORE(lock, flags)       \
    do                                            \
    {                                             \
        spinlock_release(&(lock));                \
        if ((flags) & RFLAGS_IF)                  \
            __asm__ volatile("sti" ::: "memory"); \
    } while (0)
