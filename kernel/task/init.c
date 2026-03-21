#include <debug.h>
#include <task/process.h>
#include <lib/elf.h>
#include <mem/pmm.h>
#include <mem/vmm.h>
#include <drivers/terminal.h>
#include <mem/heap.h>
#include <sys/fcntl.h>
#include <lib/string.h>

static file_descriptor_t *alloc_console_fd(vfs_inode_t *inode, int flags)
{
    if (!inode)
        return nullptr;
    file_descriptor_t *desc = kmalloc(sizeof(file_descriptor_t));
    if (!desc)
        return nullptr;
    memset(desc, 0, sizeof(file_descriptor_t));
    desc->inode  = inode;
    desc->offset = 0;
    desc->flags  = flags;
    desc->ref    = 1;
    if (__atomic_load_n(&inode->ref, __ATOMIC_RELAXED) == 0)
        vfs_open(inode);
    __atomic_add_fetch(&inode->ref, 1, __ATOMIC_RELAXED);
    return desc;
}

void init_process_entry(void)
{
    uint64_t cr3;
    __asm__ volatile("mov %0, cr3" : "=r"(cr3));
    pml4_t pml4 = (pml4_t)cr3;

    elf_load_result_t elf_result;
    if (!elf_load_ex("/bin/init", &elf_result, pml4)) {
        panic("Failed to load /bin/init");
    }
    current_process->heap_end = elf_result.max_vaddr;

    vfs_inode_t *console = vfs_resolve_path("/dev/console");
    if (console) {
        // fd 0: stdin
        int stdio_opened             = 0;
        current_process->fd_table[0] = alloc_console_fd(console, O_RDONLY);
        if (current_process->fd_table[0])
            stdio_opened++;

        // fd 1: stdout
        current_process->fd_table[1] = alloc_console_fd(console, O_WRONLY);
        if (current_process->fd_table[1])
            stdio_opened++;

        // fd 2: stderr
        current_process->fd_table[2] = alloc_console_fd(console, O_WRONLY);
        if (current_process->fd_table[2])
            stdio_opened++;

        if (stdio_opened == 0) {
            vfs_release(console);
        }
    } else {
        boot_message(WARNING, "Failed to open /dev/console for init process");
    }

    // Allocate user stack
    uint64_t stack_top  = 0x7FFFFFFFF000;
    uint64_t stack_size = 4 * 4096;
    uint64_t stack_base = stack_top - stack_size;

    for (uint64_t addr = stack_base; addr < stack_top; addr += 4096) {
        void *phys = pmm_alloc_page();
        if (!phys) {
            panic("Failed to allocate init stack page");
        }
        vmm_map_page(pml4, addr, (uint64_t)phys, PTE_PRESENT | PTE_WRITABLE | PTE_USER);
    }

    // Build the initial stack with auxiliary vector (required by ld.so).
    // Since init's PML4 is the current one, we can write directly.
    uint64_t sp = stack_top & ~0xFul;

    // Auxiliary vector (pushed bottom-up, AT_NULL first)
    sp -= sizeof(uint64_t); *(uint64_t *)sp = 0;                        // AT_NULL value
    sp -= sizeof(uint64_t); *(uint64_t *)sp = AT_NULL;
    sp -= sizeof(uint64_t); *(uint64_t *)sp = PAGE_SIZE;                // AT_PAGESZ value
    sp -= sizeof(uint64_t); *(uint64_t *)sp = AT_PAGESZ;
    sp -= sizeof(uint64_t); *(uint64_t *)sp = elf_result.interp_base;   // AT_BASE value
    sp -= sizeof(uint64_t); *(uint64_t *)sp = AT_BASE;
    sp -= sizeof(uint64_t); *(uint64_t *)sp = elf_result.entry;         // AT_ENTRY value
    sp -= sizeof(uint64_t); *(uint64_t *)sp = AT_ENTRY;
    sp -= sizeof(uint64_t); *(uint64_t *)sp = elf_result.phnum;         // AT_PHNUM value
    sp -= sizeof(uint64_t); *(uint64_t *)sp = AT_PHNUM;
    sp -= sizeof(uint64_t); *(uint64_t *)sp = elf_result.phent;         // AT_PHENT value
    sp -= sizeof(uint64_t); *(uint64_t *)sp = AT_PHENT;
    sp -= sizeof(uint64_t); *(uint64_t *)sp = elf_result.phdr_vaddr;    // AT_PHDR value
    sp -= sizeof(uint64_t); *(uint64_t *)sp = AT_PHDR;

    sp -= sizeof(uint64_t); *(uint64_t *)sp = 0;   // envp terminator
    sp -= sizeof(uint64_t); *(uint64_t *)sp = 0;   // argv terminator
    sp -= sizeof(uint64_t); *(uint64_t *)sp = 0;   // argc = 0

    // If dynamic: enter the interpreter instead of the program directly
    uint64_t entry_point = elf_result.interp_entry ? elf_result.interp_entry : elf_result.entry;

    // Jump to user mode
    uint64_t user_cs = 0x20 | 3;
    uint64_t user_ss = 0x18 | 3;
    uint64_t rflags  = 0x202;

    __asm__ volatile(
        "cli\n"
        "swapgs\n"
        "mov ds, %0\n"
        "mov es, %0\n"
        "mov fs, %0\n"
        "mov gs, %0\n"
        "push %1\n"// SS
        "push %2\n"// RSP
        "push %3\n"// RFLAGS
        "push %4\n"// CS
        "push %5\n"// RIP
        "iretq\n"
        :
        : "r"(user_ss), "r"(user_ss), "r"(sp), "r"(rflags), "r"(user_cs), "r"(entry_point)
        : "memory");
}

void process_spawn_init(void)
{
    process_t *init_proc = process_create("init");
    if (!init_proc) {
        boot_message(ERROR, "Failed to create init process");
        return;
    }
    init_process      = init_proc;
    init_proc->parent = kernel_process;

    // Set init process PML4 to current kernel PML4
    uint64_t cr3;
    __asm__ volatile("mov %0, cr3" : "=r"(cr3));
    init_proc->pml4 = (pml4_t)cr3;

    thread_t *t = thread_create(init_proc, init_process_entry, false);
    if (!t) {
        boot_message(ERROR, "Failed to create init thread");
        return;
    }
}
