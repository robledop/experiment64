#include "debug.h"
#include "elf.h"
#include "limine.h"
#include "terminal.h"
#include "cpu.h"
#include "string.h"
#include <kernel.h>
#include <stdarg.h>
#include <stdbool.h>

__attribute__((used, section(".requests"))) static volatile struct limine_kernel_file_request kernel_file_request = {
    .id = LIMINE_KERNEL_FILE_REQUEST,
    .revision = 0};

static Elf64_Shdr *elf_section_headers = NULL;
static char *strtab = NULL;
static uint64_t strtab_size = 0;
static Elf64_Sym *symtab = NULL;
static uint64_t symtab_size = 0;

void debug_init(void)
{
    boot_message(INFO,"DEBUG: Initializing debug symbols...");
    if (kernel_file_request.response == NULL || kernel_file_request.response->kernel_file == NULL)
    {
        boot_message(ERROR,"DEBUG: No kernel file found.");
        return;
    }

    struct limine_file *kernel_file = kernel_file_request.response->kernel_file;
    if (kernel_file->address == NULL)
    {
        boot_message(ERROR,"DEBUG: Kernel file address is NULL.");
        return;
    }
    boot_message(INFO,"DEBUG: Kernel file at %p, size %lx", kernel_file->address, kernel_file->size);

    Elf64_Ehdr *ehdr = (Elf64_Ehdr *)kernel_file->address;

    if (ehdr->e_ident[0] != 0x7F || ehdr->e_ident[1] != 'E' || ehdr->e_ident[2] != 'L' || ehdr->e_ident[3] != 'F')
    {
        boot_message(ERROR,"DEBUG: Kernel file is not a valid ELF.");
        return;
    }

    if (ehdr->e_shoff + ehdr->e_shnum * sizeof(Elf64_Shdr) > kernel_file->size)
    {
        boot_message(ERROR,"DEBUG: Section headers out of bounds.");
        return;
    }

    elf_section_headers = (Elf64_Shdr *)((uint8_t *)ehdr + ehdr->e_shoff);
    boot_message(INFO,"DEBUG: Section headers at %p", elf_section_headers);

    Elf64_Shdr *symtab_shdr = NULL;

    for (int i = 0; i < ehdr->e_shnum; i++)
    {
        Elf64_Shdr *shdr = &elf_section_headers[i];
        if (shdr->sh_type == SHT_SYMTAB)
        {
            symtab_shdr = shdr;
            break;
        }
    }

    if (symtab_shdr)
    {
        boot_message(INFO,"DEBUG: Symtab section found at index %ld", symtab_shdr - elf_section_headers);
        if (symtab_shdr->sh_offset + symtab_shdr->sh_size > kernel_file->size)
        {
            boot_message(ERROR,"DEBUG: Symbol table out of bounds.");
            return;
        }

        symtab = (Elf64_Sym *)((uint8_t *)ehdr + symtab_shdr->sh_offset);
        symtab_size = symtab_shdr->sh_size / sizeof(Elf64_Sym);
        boot_message(INFO,"DEBUG: Symtab at %p, size %ld", symtab, symtab_size);

        if (symtab_shdr->sh_link < ehdr->e_shnum)
        {
            Elf64_Shdr *strtab_shdr = &elf_section_headers[symtab_shdr->sh_link];
            if (strtab_shdr->sh_type == SHT_STRTAB)
            {
                if (strtab_shdr->sh_offset + strtab_shdr->sh_size > kernel_file->size)
                {
                    boot_message(ERROR,"DEBUG: String table out of bounds.");
                    symtab = NULL;
                    return;
                }
                strtab = (char *)((uint8_t *)ehdr + strtab_shdr->sh_offset);
                strtab_size = strtab_shdr->sh_size;
                boot_message(INFO,"DEBUG: Strtab at %p, size %ld", strtab, strtab_size);
            }
        }
    }

    if (symtab && strtab)
    {
        boot_message(INFO,"DEBUG: Symbols loaded.");
    }
}

static const char *get_symbol_name(uint64_t address, uint64_t *offset)
{
    if (!symtab || !strtab)
        return NULL;

    for (uint64_t i = 0; i < symtab_size; i++)
    {
        Elf64_Sym *sym = &symtab[i];
        if (address >= sym->st_value && address < sym->st_value + sym->st_size)
        {
            *offset = address - sym->st_value;
            if (sym->st_name < strtab_size)
                return strtab + sym->st_name;
        }
    }
    return NULL;
}

void panic(const char *fmt, ...)
{
    __asm__ volatile("cli");

    boot_message(INFO,"\n" KRED "PANIC: "); // Note: We are not handling varargs properly here because we lack vprintf.
    // In a real implementation, we should add vprintf to terminal.c
    boot_message(INFO,fmt);

    boot_message(INFO,KRESET "\n");

    stack_trace();

#ifdef TEST_MODE
    shutdown();
#endif

    hcf();
}

void stack_trace(void)
{
    printk(KBWHT "Stack trace:\n" KRESET);

    struct stack_frame
    {
        struct stack_frame *rbp;
        uint64_t rip;
    };

    struct stack_frame *stack = (struct stack_frame *)__builtin_frame_address(0);

    while (stack)
    {
        uint64_t offset = 0;
        const char *symbol = get_symbol_name(stack->rip, &offset);

        if (symbol)
        {
            printk("  [%p] <%s+%p>\n", (void *)stack->rip, symbol, (void *)offset);
        }
        else
        {
            printk("  [%p]\n", (void *)stack->rip);
        }

        stack = stack->rbp;

        // Sanity check to avoid infinite loops or garbage
        if ((uint64_t)stack < 0xFFFF800000000000)
            break;
    }
}
