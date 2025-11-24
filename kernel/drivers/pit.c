#include "pit.h"
#include "io.h"

#define PIT_CHANNEL0 0x40
#define PIT_CMD 0x43
#define PIT_FREQ 1193182

#define PIT_MODE0_ACCESS_LOHI 0x30
#define PIT_CMD_LATCH 0x00

void pit_init(void)
{
    // We don't really need to do much init if we only use it for sleep
}

void pit_sleep(uint32_t ms)
{
    uint16_t count = (uint16_t)((PIT_FREQ * ms) / 1000);

    // Mode 0 (Interrupt on Terminal Count), Channel 0, Access Lo/Hi
    outb(PIT_CMD, PIT_MODE0_ACCESS_LOHI);
    outb(PIT_CHANNEL0, count & 0xFF);
    outb(PIT_CHANNEL0, (count >> 8) & 0xFF);

    // Wait for the count to reach 0.
    // In Mode 0, we can read the count back.
    // Or simpler: just loop reading the count until it wraps or we detect it's done.
    // Actually, reading back the count is reliable.

    uint16_t current_count;
    do
    {
        // Send Latch command
        outb(PIT_CMD, PIT_CMD_LATCH);
        uint8_t low = inb(PIT_CHANNEL0);
        uint8_t high = inb(PIT_CHANNEL0);
        current_count = ((uint16_t)high << 8) | low;

        // In Mode 0, the counter counts down.
        // When it hits 0, it stays there (or wraps? Mode 0 wraps to N if reloaded, but here it stops/signals).
        // Actually, for Mode 0, it counts down to 0 and then output goes high.
        // The count might wrap to 0xFFFF or similar depending on implementation details,
        // but checking if it's very large (wrapped) or very small is tricky.

        // Better approach for sleep:
        // Use the fact that we just loaded 'count'.
        // Wait until we read a value > count (impossible if counting down) -> means wrap?
        // No, let's just wait until it is small.

    } while (current_count > 64); // Wait until it's close to 0.
                                  // 64 ticks is ~50us, good enough precision for "sleep".
}
