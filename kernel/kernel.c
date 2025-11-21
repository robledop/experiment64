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
#include <ide.h>
#include <string.h>
#include "test.h"

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

    // Initialize VMM
    pml4_t kernel_pml4 = vmm_new_pml4();
    if (kernel_pml4 != NULL)
    {
        vmm_switch_pml4(kernel_pml4);
        printf("VMM Initialized.\n");
    }
    else
    {
        printf("VMM Initialization Failed.\n");
        hcf();
    }

#ifdef TEST_MODE
    run_tests();
    hcf();
#endif

    ide_init();
    printf("IDE Initialized.\n");
    for (int i = 0; i < 4; i++)
    {
        if (ide_devices[i].exists)
        {
            printf("IDE Drive %d: %s - %d Sectors\n", i, ide_devices[i].model, ide_devices[i].size);
        }
    }

    // Test IDE Write/Read
    if (ide_devices[0].exists)
    {
        printf("Testing IDE Drive 0...\n");
        uint8_t write_buf[512];
        uint8_t read_buf[512];

        // Fill with pattern
        for (int i = 0; i < 512; i++)
            write_buf[i] = (uint8_t)i;

        // Write to sector 2
        ide_write_sectors(0, 2, 1, write_buf);

        // Clear read buffer
        memset(read_buf, 0, 512);

        // Read from sector 2
        ide_read_sectors(0, 2, 1, read_buf);

        // Verify
        if (memcmp(write_buf, read_buf, 512) == 0)
        {
            printf("IDE Test Passed: Data matches!\n");
        }
        else
        {
            printf("IDE Test Failed: Data mismatch!\n");
            printf("Expected: %x, Got: %x\n", write_buf[0], read_buf[0]);
        }
    }

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
