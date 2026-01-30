#include <arch/x86_64/port_io.h>

void sys_shutdown()
{
    outw(0x604, 0x2000); // qemu
    outw(0x4004, 0x3400); // VirtualBox
    outw(0xB004, 0x2000); // Bochs
    outw(0x600, 0x34); // Cloud hypervisors

    hlt();
}