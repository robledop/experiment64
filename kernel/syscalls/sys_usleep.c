#include <arch/x86_64/apic.h>
#include <drivers/tsc.h>

int sys_sleep(uint64_t milliseconds);

// ReSharper disable once CppDFAConstantFunctionResult
int sys_usleep(uint64_t usec)
{
    if (usec == 0)
        return 0;

    constexpr uint64_t tick_us = 1000000ull / TIMER_FREQUENCY_HZ;
    if (usec >= tick_us)
    {
        uint64_t ms = usec / 1000;
        if (usec % 1000)
            ms++;
        return sys_sleep(ms);
    }

    const uint64_t ns = usec * 1000;
    tsc_sleep_ns(ns);
    return 0;
}
