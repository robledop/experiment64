#include <stdint.h>
#include <stddef.h>
#include "gdt.h"
#include "idt.h"
#include "terminal.h"
#include "framebuffer.h"
#include "bmp.h"
#include "cpu.h"
#include "apic.h"
#include "uart.h"
#include "pmm.h"
#include "vmm.h"
#include "heap.h"
#include "bio.h"
#include "ide.h"
#include "keyboard.h"
#include "vfs.h"
#include "syscall.h"
#include "process.h"
#include "boot.h"
#include "smp.h"
#include "io.h"
#include "debug.h"
#include "tsc.h"
#include "console.h"
#include "devfs.h"
#include "kernel.h"
#include "pci.h"
#include "storage.h"
#ifdef TEST_MODE
#include "test.h"
#endif
#ifdef KASAN
#include "kasan.h"
#endif

void shutdown()
{
    // Exit QEMU
    // Try 0x501 which is common default
    outb(ISA_DEBUG_EXIT_PORT, ISA_DEBUG_EXIT_CMD);
    outw(ISA_DEBUG_EXIT_PORT, ISA_DEBUG_EXIT_CMD);
    outd(ISA_DEBUG_EXIT_PORT, ISA_DEBUG_EXIT_CMD);

    // Try 0xf4 as well
    outb(QEMU_EXIT_PORT, QEMU_EXIT_CMD);
    outw(QEMU_EXIT_PORT, QEMU_EXIT_CMD);
    outd(QEMU_EXIT_PORT, QEMU_EXIT_CMD);

    outw(QEMU_SHUTDOWN_PORT, QEMU_SHUTDOWN_CMD);   // qemu
    outw(VBOX_SHUTDOWN_PORT, VBOX_SHUTDOWN_CMD);   // VirtualBox
    outw(BOCHS_SHUTDOWN_PORT, BOCHS_SHUTDOWN_CMD); // Bochs
    outw(CLOUD_SHUTDOWN_PORT, CLOUD_SHUTDOWN_CMD); // Cloud hypervisors
}

static void kernel_splash_ascii(void)
{
    terminal_clear(0x00000000);
    printk("\033[1;32m\n");
    printk("                           _                      _      __   _  _   \n");
    printk("  _____  ___ __   ___ _ __(_)_ __ ___   ___ _ __ | |_   / /_ | || |  \n");
    printk(" / _ \\ \\/ / '_ \\ / _ \\ '__| | '_ ` _ \\ / _ \\ '_ \\| __| | '_ \\| || |_ \n");
    printk("|  __/  \\  <| |_) |  __/ |  | | | | | | |  __/ | | | |_  | (_) |__   _|\n");
    printk(" \\___|_/\\_\\ .__/ \\___|_|  |_|_| |_| |_|\\___|_| |_|\\__|  \\___/   |_|  \n");
    printk("         |_|                                                         \n");
    printk("\n\033[0m");
}

void kernel_splash(void)
{
    struct limine_framebuffer *fb = framebuffer_current();
    if (!fb)
    {
        kernel_splash_ascii();
        return;
    }

    terminal_clear(0x00000000);

    uint32_t *pixels = nullptr;
    uint32_t width = 0;
    uint32_t height = 0;
    if (bitmap_load_argb("/var/logo.bmp", &pixels, &width, &height) != 0 || !pixels)
    {
        kernel_splash_ascii();
        return;
    }

    int cursor_x, cursor_y_start;
    terminal_get_cursor(&cursor_x, &cursor_y_start);
    cursor_y_start = 0;

    constexpr uint32_t splash_bottom_margin = 13;
    constexpr uint32_t origin_x = 0;
    const uint32_t origin_y = (uint32_t)cursor_y_start;

    const uint32_t max_width = fb->width - origin_x;
    const uint32_t max_height = (fb->height > origin_y) ? (fb->height - origin_y) : 0;

    const uint32_t draw_width = (width > max_width) ? max_width : width;
    const uint32_t draw_height = (height > max_height) ? max_height : height;

    for (uint32_t row = 0; row < draw_height; row++)
    {
        framebuffer_blit_span32(origin_y + row, origin_x, &pixels[row * width], draw_width);
    }

    kfree(pixels);

    uint32_t cursor_y = origin_y + draw_height + splash_bottom_margin;
    if (cursor_y >= fb->height)
        cursor_y = fb->height ? (fb->height - 1) : 0;
    terminal_set_cursor(0, (int)cursor_y);
}

[[noreturn]]
void _start(void) // NOLINT(*-reserved-identifier)
{
    enable_simd();
    uart_init();
    boot_init();
    boot_init_terminal();
    smp_init_cpu0();
    gdt_init();
    idt_init();
    debug_init();
    apic_init();
    tsc_init();
    smp_boot_aps();
    syscall_init();
    uint64_t hhdm_offset = boot_get_hhdm_offset();
    pmm_init(hhdm_offset);
    vmm_init(hhdm_offset);
#ifdef KASAN
    kasan_early_init(hhdm_offset, pmm_get_highest_addr());
#endif
    heap_init(hhdm_offset);
    keyboard_init();
    process_init();
    pci_scan();
    storage_init();
    bio_init();
    vfs_init();
    devfs_init();
    console_init();

    // Set up framebuffer with Write-Combining for better performance
    struct limine_framebuffer *fb = framebuffer_current();
    if (fb)
    {
        uint64_t fb_size = fb->pitch * fb->height;
        uint64_t fb_phys = (uint64_t)fb->address - hhdm_offset;

        // Set MTRR to WC (this overrides the UC setting)
        cpu_set_mtrr_wc(fb_phys, fb_size);

        // Also update page tables with WC attribute
        vmm_remap_wc((uint64_t)fb->address, fb_size);

        printk("Framebuffer: virt=0x%lx phys=0x%lx size=%lu bytes (WC)\n",
               (uint64_t)fb->address, fb_phys, fb_size);
    }

    vfs_mount_root();

#ifdef TEST_MODE
    run_tests();
#else
    kernel_splash();
    process_spawn_init();
#endif

    while (1)
    {
        __asm__ volatile("hlt");
    }
}
