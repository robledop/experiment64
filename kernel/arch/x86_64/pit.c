#include <drivers/pit.h>
#include <arch/x86_64/port_io.h>

#define PIT_CHANNEL0 0x40
#define PIT_CMD 0x43
#define PIT_FREQ 1193182

#define PIT_MODE0_ACCESS_LOHI 0x30
#define PIT_CMD_LATCH 0x00

void pit_sleep(uint32_t ms)
{
    uint16_t count = (uint16_t)((PIT_FREQ * ms) / 1000);

    // Mode 0 (Interrupt on Terminal Count), Channel 0, Access Lo/Hi
    outb(PIT_CMD, PIT_MODE0_ACCESS_LOHI);
    outb(PIT_CHANNEL0, count & 0xFF);
    outb(PIT_CHANNEL0, (count >> 8) & 0xFF);

    uint16_t current_count;
    do {
        // Send Latch command
        outb(PIT_CMD, PIT_CMD_LATCH);
        uint8_t low   = inb(PIT_CHANNEL0);
        uint8_t high  = inb(PIT_CHANNEL0);
        current_count = ((uint16_t)high << 8) | low;
    } while (current_count > 64); // Wait until it's close to 0.
}