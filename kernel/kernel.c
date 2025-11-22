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
#include "process.h"
#include "elf.h"
#include "io.h"

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

void init_process_entry(void)
{
    outb(0x3F8, 'X');
    outb(0x3F8, '\n');
    printf("init_process_entry called\n");
    uint64_t entry_point;
    uint64_t cr3;
    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
    if (!elf_load("/init", &entry_point, (pml4_t)cr3))
    {
        printf("Failed to load /init\n");
        while (1)
            __asm__("hlt");
    }

    // Allocate user stack
    uint64_t stack_top = 0x7FFFFFFFF000;
    uint64_t stack_size = 4 * 4096;
    uint64_t stack_base = stack_top - stack_size;

    // Map stack
    // uint64_t cr3; // Already declared
    // __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
    pml4_t pml4 = (pml4_t)cr3;

    for (uint64_t addr = stack_base; addr < stack_top; addr += 4096)
    {
        void *phys = pmm_alloc_page();
        vmm_map_page(pml4, addr, (uint64_t)phys, PTE_PRESENT | PTE_WRITABLE | PTE_USER);
    }

    // Jump to user mode
    uint64_t user_cs = 0x20 | 3;
    uint64_t user_ss = 0x18 | 3;
    uint64_t rflags = 0x202;

    printf("Jumping to user mode at %lx\n", entry_point);

    __asm__ volatile(
        "mov %0, %%ds\n"
        "mov %0, %%es\n"
        "mov %0, %%fs\n"
        "mov %0, %%gs\n"
        "pushq %1\n" // SS
        "pushq %2\n" // RSP
        "pushq %3\n" // RFLAGS
        "pushq %4\n" // CS
        "pushq %5\n" // RIP
        "iretq\n"
        :
        : "r"(user_ss), "r"(user_ss), "r"(stack_top), "r"(rflags), "r"(user_cs), "r"(entry_point)
        : "memory");
}

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
    syscall_init();

    // Initialize PMM
    pmm_init(hhdm_request.response->offset);
    vmm_init(hhdm_request.response->offset);
    heap_init(hhdm_request.response->offset);
    process_init();
    ide_init();
    bio_init();
    vfs_init();

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
#else
    process_t *init_proc = process_create("init");
    if (!init_proc)
        printf("Failed to create init process\n");
    thread_t *t = thread_create(init_proc, init_process_entry, false);
    if (!t)
        printf("Failed to create init thread\n");
    else
        printf("Created init thread %d\n", t->tid);
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

    while (1)
    {
        yield();
    }
}
