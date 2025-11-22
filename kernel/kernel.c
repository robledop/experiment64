#include <stdint.h>
#include <stddef.h>
#include <limine.h>
#include "font.h"
#include "gdt.h"
#include "idt.h"
#include "terminal.h"
#include "cpu.h"
#include "pic.h"
#include "apic.h"
#include "keyboard.h"
#include "uart.h"
#include "pmm.h"
#include "vmm.h"
#include "heap.h"
#include "bio.h"
#include "fat32.h"
#include "test.h"
#include "ide.h"
#include "string.h"
#include "vfs.h"
#include "syscall.h"

__attribute__((used, section(".requests_start"))) static volatile LIMINE_REQUESTS_START_MARKER;
__attribute__((used, section(".requests_end"))) static volatile LIMINE_REQUESTS_END_MARKER;

// Set the base revision to 2, this is recommended as of late 2024
__attribute__((used, section(".requests"))) static volatile LIMINE_BASE_REVISION(2);

__attribute__((used, section(".requests"))) volatile struct limine_framebuffer_request framebuffer_request = {
    .id = LIMINE_FRAMEBUFFER_REQUEST,
    .revision = 0};

__attribute__((used, section(".requests"))) volatile struct limine_hhdm_request hhdm_request = {
    .id = LIMINE_HHDM_REQUEST,
    .revision = 0};

void _start(void)
{
    // Ensure the bootloader actually understands our base revision (see spec).
    if (LIMINE_BASE_REVISION_SUPPORTED == false)
    {
        hcf();
    }

    enable_sse();

    uart_init();
    gdt_init();
    idt_init();
    syscall_init();
    apic_init();
    keyboard_init();

    if (framebuffer_request.response == NULL || framebuffer_request.response->framebuffer_count < 1)
    {
        hcf();
    }

    struct limine_framebuffer *framebuffer = framebuffer_request.response->framebuffers[0];

    terminal_init(framebuffer);

    if (hhdm_request.response == NULL)
    {
        hcf();
    }

    pmm_init(hhdm_request.response->offset);
    vmm_init(hhdm_request.response->offset);
    heap_init(hhdm_request.response->offset);
    ide_init();
    bio_init();
    vfs_init();

    // Mount FAT32 partition (Drive 0, Partition 1 - LBA 2048)
    // TODO: Use GPT to find this dynamically
    vfs_root = fat32_mount(0, 2048);
    if (vfs_root)
    {
        printf("VFS: Mounted FAT32 on /\n");
    }
    else
    {
        printf("VFS: Failed to mount FAT32\n");
    }

#ifdef TEST_MODE
    run_tests();
#endif

    vmm_finalize();

    terminal_clear(0xFF000088); // Dark Blue Background

    terminal_set_cursor(10, 10);
    terminal_set_color(0xFFFFFFFF);
    printf("Hello World!\n");

    terminal_set_cursor(10, 20);
    terminal_set_color(0xFF00FF00);
    printf("Limine Bootloader\n");

    terminal_set_cursor(10, 30);
    terminal_set_color(0xFF00FFFF);
    printf("SSE Enabled\n");

    terminal_set_cursor(10, 40);
    terminal_set_color(0xFFFFFF00);
    printf("GDT & IDT Initialized\n");
    printf("Keyboard Initialized. Type something!\n");

    hcf();
}
