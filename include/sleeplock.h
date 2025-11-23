#pragma once

#include "spinlock.h"
#include <stdbool.h>

typedef struct
{
    spinlock_t lock;
    bool locked;
    int pid; // For debugging
    const char *name;
} sleeplock_t;

void sleeplock_init(sleeplock_t *lk, const char *name);
void sleeplock_acquire(sleeplock_t *lk);
void sleeplock_release(sleeplock_t *lk);
bool sleeplock_holding(sleeplock_t *lk);
