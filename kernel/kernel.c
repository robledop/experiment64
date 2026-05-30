/**
 * @file kernel.c
 * @brief Post-Limine C entry point (_start) and the master init sequence.
 *
 * Limine hands control to _start() in the higher half. From here the kernel
 * brings each subsystem up in a deliberate, load-bearing order: CPU features,
 * per-CPU/GDT/IDT, APIC + timing, SMP, memory (PMM/VMM/heap), input, the VFS
 * and devices, then the root mount. It ends by either spawning the first
 * userland process (process_spawn_init(), see kernel/task/init.c) or, under
 * -DTEST_MODE, running the in-kernel test suite (run_tests(), see
 * kernel/tests/test_runner.c) before halting forever.
 *
 * The ordering caveat that bites readers: smp_init_cpu0() must precede
 * gdt_init(). smp_init_cpu0() (kernel/arch/x86_64/smp.c) writes MSR_GS_BASE so
 * get_cpu() resolves; gdt_init() then loads a null GS selector which zeroes the
 * GS base, and re-establishes it via wrmsr afterward. See gdt_init() in
 * kernel/arch/x86_64/gdt.c.
 */
#include <stdint.h>
#include <arch/x86_64/gdt.h>
#include <arch/x86_64/idt.h>
#include <drivers/terminal.h>
#include <drivers/framebuffer.h>
#include <lib/bmp.h>
#include <arch/x86_64/cpu.h>
#include <arch/x86_64/apic.h>
#include <drivers/uart.h>
#include <mem/pmm.h>
#include <mem/vmm.h>
#include <mem/heap.h>
#include <io/bio.h>
#include <drivers/keyboard.h>
#include <drivers/mouse.h>
#include <fs/vfs.h>
#include <sys/syscall.h>
#include <task/process.h>
#include <boot.h>
#include <arch/x86_64/smp.h>
#include <arch/x86_64/port_io.h>
#include <debug.h>
#include <drivers/tsc.h>
#include <drivers/console.h>
#include <fs/devfs.h>
#include <ipc/shm.h>
#include <kernel.h>
#include <drivers/pci.h>
#include <io/storage.h>
#ifdef TEST_MODE
#include <tests/test.h>
#endif

void shutdown()
{
    // Exit QEMU
    // Try 0x501 which is common default
    outb(ISA_DEBUG_EXIT_PORT, ISA_DEBUG_EXIT_CMD);
    outw(ISA_DEBUG_EXIT_PORT, ISA_DEBUG_EXIT_CMD);
    outl(ISA_DEBUG_EXIT_PORT, ISA_DEBUG_EXIT_CMD);

    // Try 0xf4 as well
    outb(QEMU_EXIT_PORT, QEMU_EXIT_CMD);
    outw(QEMU_EXIT_PORT, QEMU_EXIT_CMD);
    outl(QEMU_EXIT_PORT, QEMU_EXIT_CMD);

    outw(QEMU_SHUTDOWN_PORT, QEMU_SHUTDOWN_CMD);   // qemu
    outw(VBOX_SHUTDOWN_PORT, VBOX_SHUTDOWN_CMD);   // VirtualBox
    outw(BOCHS_SHUTDOWN_PORT, BOCHS_SHUTDOWN_CMD); // Bochs
    outw(CLOUD_SHUTDOWN_PORT, CLOUD_SHUTDOWN_CMD); // Cloud hypervisors
}

static void kernel_splash_ascii(void)
{
    terminal_clear(0x00000000);
    printk("\033[1;32m");
    printk("experiment64");
    printk("\n\033[0m");
}

void kernel_splash(void)
{
    struct limine_framebuffer *fb = framebuffer_current();
    if (!fb) {
        kernel_splash_ascii();
        return;
    }

    terminal_clear(0x00000000);

    uint32_t *pixels = nullptr;
    uint32_t width   = 0;
    uint32_t height  = 0;
    if (bitmap_load_argb("/var/logo.bmp", &pixels, &width, &height) != 0 || !pixels) {
        kernel_splash_ascii();
        return;
    }

    int cursor_x, cursor_y_start;
    terminal_get_cursor(&cursor_x, &cursor_y_start);
    cursor_y_start = 0;

    constexpr uint32_t splash_bottom_margin = 13;
    constexpr uint32_t origin_x             = 0;
    const uint32_t origin_y                 = (uint32_t)cursor_y_start;

    const uint32_t max_width  = fb->width - origin_x;
    const uint32_t max_height = (fb->height > origin_y) ? (fb->height - origin_y) : 0;

    const uint32_t draw_width  = (width > max_width) ? max_width : width;
    const uint32_t draw_height = (height > max_height) ? max_height : height;

    for (uint32_t row = 0; row < draw_height; row++) {
        framebuffer_blit_span32(origin_y + row, origin_x, &pixels[row * width], draw_width);
    }

    kfree(pixels);
    terminal_sync_backbuffer();

    uint32_t cursor_y = origin_y + draw_height + splash_bottom_margin;
    if (cursor_y >= fb->height)
        cursor_y = fb->height ? (fb->height - 1) : 0;
    terminal_set_cursor(0, (int)cursor_y);
}

[[noreturn]]
void _start(void)
{
    enable_simd();
    enable_fsgsbase();
    uart_init();
    boot_init();
    boot_init_terminal();
    smp_init_cpu0(); // sets MSR_GS_BASE; MUST precede gdt_init (kernel/arch/x86_64/smp.c)
    gdt_init();      // null GS selector zeroes GS base, then re-set via wrmsr (kernel/arch/x86_64/gdt.c)
    idt_init();
    debug_init();
    apic_init();
    tsc_init();
    smp_boot_aps();
    syscall_init();
    uint64_t hhdm_offset = boot_get_hhdm_offset();
    pmm_init(hhdm_offset);
    vmm_init(hhdm_offset);
    heap_init(hhdm_offset);
    terminal_init_backbuffer();
    keyboard_init();
    mouse_init();
    process_init();
    pci_scan();
    storage_init();
    bio_init();
    vfs_init();
    devfs_init();
    shm_init();
    console_init();
    vfs_mount_root();

#ifdef TEST_MODE
    run_tests();
#else
    kernel_splash();
    process_spawn_init();
#endif

    while (1) {
        __asm__ volatile("hlt");
    }
}