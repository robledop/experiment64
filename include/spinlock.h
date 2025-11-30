#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "cpu.h" // For RFLAGS_IF

typedef struct
{
    volatile bool locked;
} spinlock_t;

void spinlock_init(spinlock_t *lock);
void spinlock_acquire(spinlock_t *lock);
void spinlock_release(spinlock_t *lock);

// Interrupt-safe spinlock macros
// Usage:
//   uint64_t flags;
//   SPIN_LOCK_IRQSAVE(lock, flags);
//   ... critical section ...
//   SPIN_UNLOCK_IRQRESTORE(lock, flags);

#define SPIN_LOCK_IRQSAVE(lock, flags)                                   \
    do                                                                   \
    {                                                                    \
        __asm__ volatile("pushfq; pop %0; cli" : "=r"(flags)::"memory"); \
        spinlock_acquire(&(lock));                                       \
    } while (0)

#define SPIN_UNLOCK_IRQRESTORE(lock, flags)       \
    do                                            \
    {                                             \
        spinlock_release(&(lock));                \
        if ((flags) & RFLAGS_IF)                  \
            __asm__ volatile("sti" ::: "memory"); \
    } while (0)
