#include <task/process.h>

#define TIMER_TICK_MS 10

int sys_sleep(uint64_t milliseconds)
{
    uint64_t start = scheduler_ticks;
    uint64_t ticks = milliseconds / TIMER_TICK_MS;
    if (ticks == 0)
        ticks = 1;

    while (scheduler_ticks < start + ticks)
    {
        schedule();
    }
    return 0;
}
