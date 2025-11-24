#include "process.h"
#include "elf.h"
#include "pmm.h"
#include "vmm.h"
#include "terminal.h"
#include "heap.h"

void init_process_entry(void)
{
    uint64_t entry_point;
    uint64_t max_vaddr = 0;
    uint64_t cr3;
    __asm__ volatile("mov %0, cr3" : "=r"(cr3));
    if (!elf_load("/bin/init", &entry_point, &max_vaddr, (pml4_t)cr3))
    {
        boot_message(ERROR, "Failed to load /bin/init");
        while (1)
            __asm__("hlt");
    }
    current_process->heap_end = max_vaddr;

    // Open /dev/console for stdin, stdout, stderr
    vfs_inode_t *console = vfs_resolve_path("/dev/console");
    if (console)
    {
        // fd 0: stdin
        current_process->fd_table[0] = kmalloc(sizeof(file_descriptor_t));
        current_process->fd_table[0]->inode = console;
        current_process->fd_table[0]->offset = 0;
        vfs_open(console);

        // fd 1: stdout
        current_process->fd_table[1] = kmalloc(sizeof(file_descriptor_t));
        current_process->fd_table[1]->inode = console;
        current_process->fd_table[1]->offset = 0;

        // fd 2: stderr
        current_process->fd_table[2] = kmalloc(sizeof(file_descriptor_t));
        current_process->fd_table[2]->inode = console;
        current_process->fd_table[2]->offset = 0;
    }
    else
    {
        boot_message(WARNING, "Failed to open /dev/console for init process");
    }

    // Allocate user stack
    uint64_t stack_top = 0x7FFFFFFFF000;
    uint64_t stack_size = 4 * 4096;
    uint64_t stack_base = stack_top - stack_size;

    // Map stack
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

    __asm__ volatile(
        "cli\n"
        "swapgs\n"
        "mov ds, %0\n"
        "mov es, %0\n"
        "mov fs, %0\n"
        "mov gs, %0\n"
        "push %1\n" // SS
        "push %2\n" // RSP
        "push %3\n" // RFLAGS
        "push %4\n" // CS
        "push %5\n" // RIP
        "iretq\n"
        :
        : "r"(user_ss), "r"(user_ss), "r"(stack_top), "r"(rflags), "r"(user_cs), "r"(entry_point)
        : "memory");
}

void process_spawn_init(void)
{
    process_t *init_proc = process_create("init");
    if (!init_proc)
        boot_message(ERROR, "Failed to create init process");

    // Set init process PML4 to current kernel PML4
    uint64_t cr3;
    __asm__ volatile("mov %0, cr3" : "=r"(cr3));
    init_proc->pml4 = (pml4_t)cr3;

    thread_t *t = thread_create(init_proc, init_process_entry, false);
    if (!t)
        boot_message(ERROR, "Failed to create init thread");
}
