#include <debug.h>
#include <lib/elf.h>
#include "limine.h"
#include <drivers/terminal.h>
#include <arch/x86_64/cpu.h>
#include <drivers/tsc.h>
#include <kernel.h>
#include <stdarg.h>

#ifdef TEST_MODE
#include <tests/test.h>
extern volatile uint64_t test_syscall_last_num;
extern volatile uint64_t test_syscall_last_arg1;
#endif

__attribute__((used, section(".requests"))) static volatile struct limine_kernel_file_request kernel_file_request = {
    .id = LIMINE_KERNEL_FILE_REQUEST,
    .revision = 0
};

static Elf64_Shdr* elf_section_headers = nullptr;
static char* strtab = nullptr;
static uint64_t strtab_size = 0;
static elf64_sym* symtab = nullptr;
static uint64_t symtab_size = 0;

#ifdef TEST_MODE
static bool panic_trap_enabled = false;
static bool panic_trap_armed = false;
static bool panic_trap_hit = false;
#endif

int panic_trap_setjmp(void)
{
#ifdef TEST_MODE
    panic_trap_enabled = true;
    panic_trap_armed = true;
    panic_trap_hit = false;
    return 0;
#else
    return 0;
#endif
}

void panic_trap_expect(void)
{
#ifdef TEST_MODE
    panic_trap_enabled = true;
    panic_trap_armed = true;
    panic_trap_hit = false;
#endif
}

void panic_trap_disable(void)
{
#ifdef TEST_MODE
    panic_trap_enabled = false;
    panic_trap_armed = false;
    panic_trap_hit = false;
#endif
}

bool panic_trap_triggered(void)
{
#ifdef TEST_MODE
    return panic_trap_hit;
#else
    return false;
#endif
}

bool panic_trap_active(void)
{
#ifdef TEST_MODE
    return panic_trap_enabled && panic_trap_armed;
#else
    return false;
#endif
}

void panic_trap_mark_hit(void)
{
#ifdef TEST_MODE
    if (panic_trap_enabled && panic_trap_armed)
    {
        panic_trap_armed = false;
        panic_trap_hit = true;
    }
#endif
}

void debug_init(void)
{
    boot_message(INFO, "DEBUG: Initializing debug symbols...");
    if (kernel_file_request.response == nullptr || kernel_file_request.response->kernel_file == nullptr)
    {
        boot_message(ERROR, "DEBUG: No kernel file found.");
        return;
    }

    struct limine_file* kernel_file = kernel_file_request.response->kernel_file;
    if (kernel_file->address == nullptr)
    {
        boot_message(ERROR, "DEBUG: Kernel file address is nullptr.");
        return;
    }
    boot_message(INFO, "DEBUG: Kernel file at %p, size %lx", kernel_file->address, kernel_file->size);

    elf64_ehdr* ehdr = (elf64_ehdr*)kernel_file->address;

    if (ehdr->e_ident[0] != 0x7F || ehdr->e_ident[1] != 'E' || ehdr->e_ident[2] != 'L' || ehdr->e_ident[3] != 'F')
    {
        boot_message(ERROR, "DEBUG: Kernel file is not a valid ELF.");
        return;
    }

    if (ehdr->e_shoff + ehdr->e_shnum * sizeof(Elf64_Shdr) > kernel_file->size)
    {
        boot_message(ERROR, "DEBUG: Section headers out of bounds.");
        return;
    }

    elf_section_headers = (Elf64_Shdr*)((uint8_t*)ehdr + ehdr->e_shoff);
    boot_message(INFO, "DEBUG: Section headers at %p", elf_section_headers);

    Elf64_Shdr* symtab_shdr = nullptr;

    for (int i = 0; i < ehdr->e_shnum; i++)
    {
        Elf64_Shdr* shdr = &elf_section_headers[i];
        if (shdr->sh_type == SHT_SYMTAB)
        {
            symtab_shdr = shdr;
            break;
        }
    }

    if (symtab_shdr)
    {
        boot_message(INFO, "DEBUG: Symtab section found at index %ld", symtab_shdr - elf_section_headers);
        if (symtab_shdr->sh_offset + symtab_shdr->sh_size > kernel_file->size)
        {
            boot_message(ERROR, "DEBUG: Symbol table out of bounds.");
            return;
        }

        symtab = (elf64_sym*)((uint8_t*)ehdr + symtab_shdr->sh_offset);
        symtab_size = symtab_shdr->sh_size / sizeof(elf64_sym);
        boot_message(INFO, "DEBUG: Symtab at %p, size %ld", symtab, symtab_size);

        if (symtab_shdr->sh_link < ehdr->e_shnum)
        {
            Elf64_Shdr* strtab_shdr = &elf_section_headers[symtab_shdr->sh_link];
            if (strtab_shdr->sh_type == SHT_STRTAB)
            {
                if (strtab_shdr->sh_offset + strtab_shdr->sh_size > kernel_file->size)
                {
                    boot_message(ERROR, "DEBUG: String table out of bounds.");
                    symtab = nullptr;
                    return;
                }
                strtab = (char*)((uint8_t*)ehdr + strtab_shdr->sh_offset);
                strtab_size = strtab_shdr->sh_size;
                boot_message(INFO, "DEBUG: Strtab at %p, size %ld", strtab, strtab_size);
            }
        }
    }

    if (symtab && strtab)
    {
        boot_message(INFO, "DEBUG: Symbols loaded.");
    }
}

static const char* get_symbol_name(uint64_t address, uint64_t* offset)
{
    if (!symtab || !strtab)
        return nullptr;

    for (uint64_t i = 0; i < symtab_size; i++)
    {
        elf64_sym* sym = &symtab[i];
        if (address >= sym->st_value && address < sym->st_value + sym->st_size)
        {
            *offset = address - sym->st_value;
            if (sym->st_name < strtab_size)
                return strtab + sym->st_name;
        }
    }
    return nullptr;
}

#if !defined(TEST_MODE)
[[noreturn]]
#endif
void panic(const char* fmt, ...)
{
    __asm__ volatile("cli");

    printk("\n" KRED "PANIC: ");

    va_list args;
    va_start(args, fmt);
    vprintk(fmt, args);
    va_end(args); // NOLINT(clang-analyzer-valist.Uninitialized)

    printk(KRESET "\n");

#ifdef TEST_MODE
    if (g_current_test_name)
    {
        printk("During test: %s\n", g_current_test_name);
        test_capture_flush();
    }
    if (g_current_test_start_ns)
    {
        uint64_t now = tsc_nanos();
        uint64_t elapsed_ms = (now > g_current_test_start_ns) ? ((now - g_current_test_start_ns) / 1000000ULL) : 0;
        printk("Test elapsed: %lums\n", (unsigned long)elapsed_ms);
    }
    printk("Last syscall: %llu arg1=0x%llx\n",
           (unsigned long long)test_syscall_last_num,
           (unsigned long long)test_syscall_last_arg1);
#endif

#ifdef TEST_MODE
    if (panic_trap_active())
        panic_trap_mark_hit();
    if (panic_trap_triggered())
        return;
#endif

    stack_trace();

#ifdef TEST_MODE
    shutdown();
#endif

    hcf();
    // ReSharper disable once CppDFAUnreachableCode
    __builtin_unreachable();
}

void stack_trace(void)
{
    printk(KBWHT "Stack trace:\n" KRESET);

    struct stack_frame
    {
        struct stack_frame* rbp;
        uint64_t rip;
    };

    auto stack = (struct stack_frame*)__builtin_frame_address(0);

    while (stack)
    {
        uint64_t offset = 0;
        const char* symbol = get_symbol_name(stack->rip, &offset);

        if (symbol)
        {
            printk("\t" KBWHT "[" KRESET "%p" KBWHT"]" KRESET" <" KBWHT "%s" KRESET "+%p>\n", (void*)stack->rip, symbol,
                   (void*)offset);
        }
        else
        {
            printk("\t" KBWHT"[" KRESET "%p" KBWHT"]" KRESET"\n", (void*)stack->rip);
        }

        stack = stack->rbp;

        // Sanity check to avoid infinite loops or garbage
        if ((uint64_t)stack < 0xFFFF800000000000)
            break;
    }
}
