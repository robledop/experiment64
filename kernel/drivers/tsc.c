#include <drivers/tsc.h>
#include <arch/x86_64/apic.h>
#include <arch/x86_64/cpu.h>
#include <drivers/pit.h>
#include <drivers/terminal.h>
#include <task/process.h>

static uint64_t tsc_frequency = 0;

void tsc_init(void)
{
    boot_message(INFO, "TSC: Calibrating...");

    // Calibrate TSC using PIT
    // Sleep for 50ms
    const uint64_t start = rdtsc();
    pit_sleep(50);
    const uint64_t end = rdtsc();

    const uint64_t diff = end - start;

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
    const uint64_t freq_mhz = tsc_frequency / 1000000;
    if (freq_mhz == 0)
        return 0;
    return (rdtsc() * 1000) / freq_mhz;
}

uint64_t tsc_monotonic_ns(void)
{
    const uint64_t ns = tsc_nanos();
    if (ns != 0)
        return ns;
    return scheduler_ticks * (1000000000ull / TIMER_FREQUENCY_HZ);
}

void tsc_sleep_ns(uint64_t ns)
{
    uint64_t start = rdtsc();
    // Use MHz to avoid 64-bit overflow.
    uint64_t freq_mhz = tsc_frequency / 1000000;
    uint64_t ticks    = (ns * freq_mhz) / 1000;
    while (rdtsc() - start < ticks) {
        __asm__ volatile("pause");
    }
}

void tsc_sleep_ms(const uint64_t ms)
{
    tsc_sleep_ns(ms * 1000000);
}
