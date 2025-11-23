#include "tsc.h"
#include "cpu.h"
#include "pit.h"
#include "terminal.h"

static uint64_t tsc_frequency = 0;

void tsc_init(void)
{
    boot_message(INFO, "TSC: Calibrating...");

    // Calibrate TSC using PIT
    // Sleep for 50ms
    uint64_t start = rdtsc();
    pit_sleep(50);
    uint64_t end = rdtsc();

    uint64_t diff = end - start;

    // diff ticks in 50ms -> freq = diff * 20
    tsc_frequency = diff * 20;

    boot_message(INFO, "TSC: Frequency detected: %ld Hz (%ld MHz)", tsc_frequency, tsc_frequency / 1000000);
}

uint64_t tsc_get_ticks(void)
{
    return rdtsc();
}

uint64_t tsc_get_freq(void)
{
    return tsc_frequency;
}

uint64_t tsc_nanos(void)
{
    if (tsc_frequency == 0)
        return 0;
    // Use MHz to avoid 64-bit overflow. Valid for ~70 days of uptime.
    uint64_t freq_mhz = tsc_frequency / 1000000;
    if (freq_mhz == 0)
        return 0;
    return (rdtsc() * 1000) / freq_mhz;
}

void tsc_sleep_ns(uint64_t ns)
{
    uint64_t start = rdtsc();
    // Use MHz to avoid 64-bit overflow.
    uint64_t freq_mhz = tsc_frequency / 1000000;
    uint64_t ticks = (ns * freq_mhz) / 1000;
    while (rdtsc() - start < ticks)
    {
        __asm__ volatile("pause");
    }
}

void tsc_sleep_ms(uint64_t ms)
{
    tsc_sleep_ns(ms * 1000000);
}
