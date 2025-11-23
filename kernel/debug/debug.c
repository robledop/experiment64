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
    printf("DEBUG: Initializing debug symbols...\n");
    if (kernel_file_request.response == NULL || kernel_file_request.response->kernel_file == NULL)
    {
        printf("DEBUG: No kernel file found.\n");
        return;
    }

    struct limine_file *kernel_file = kernel_file_request.response->kernel_file;
    if (kernel_file->address == NULL)
    {
        printf("DEBUG: Kernel file address is NULL.\n");
        return;
    }
    printf("DEBUG: Kernel file at %p, size %lx\n", kernel_file->address, kernel_file->size);

    Elf64_Ehdr *ehdr = (Elf64_Ehdr *)kernel_file->address;

    if (ehdr->e_ident[0] != 0x7F || ehdr->e_ident[1] != 'E' || ehdr->e_ident[2] != 'L' || ehdr->e_ident[3] != 'F')
    {
        printf("DEBUG: Kernel file is not a valid ELF.\n");
        return;
    }

    if (ehdr->e_shoff + ehdr->e_shnum * sizeof(Elf64_Shdr) > kernel_file->size)
    {
        printf("DEBUG: Section headers out of bounds.\n");
        return;
    }

    elf_section_headers = (Elf64_Shdr *)((uint8_t *)ehdr + ehdr->e_shoff);
    printf("DEBUG: Section headers at %p\n", elf_section_headers);

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
        printf("DEBUG: Symtab section found at index %ld\n", symtab_shdr - elf_section_headers);
        if (symtab_shdr->sh_offset + symtab_shdr->sh_size > kernel_file->size)
        {
            printf("DEBUG: Symbol table out of bounds.\n");
            return;
        }

        symtab = (Elf64_Sym *)((uint8_t *)ehdr + symtab_shdr->sh_offset);
        symtab_size = symtab_shdr->sh_size / sizeof(Elf64_Sym);
        printf("DEBUG: Symtab at %p, size %ld\n", symtab, symtab_size);

        if (symtab_shdr->sh_link < ehdr->e_shnum)
        {
            Elf64_Shdr *strtab_shdr = &elf_section_headers[symtab_shdr->sh_link];
            if (strtab_shdr->sh_type == SHT_STRTAB)
            {
                if (strtab_shdr->sh_offset + strtab_shdr->sh_size > kernel_file->size)
                {
                    printf("DEBUG: String table out of bounds.\n");
                    symtab = NULL;
                    return;
                }
                strtab = (char *)((uint8_t *)ehdr + strtab_shdr->sh_offset);
                strtab_size = strtab_shdr->sh_size;
                printf("DEBUG: Strtab at %p, size %ld\n", strtab, strtab_size);
            }
        }
    }

    if (symtab && strtab)
    {
        printf("DEBUG: Symbols loaded.\n");
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

    printf("\n" KRED "PANIC: "); // Note: We are not handling varargs properly here because we lack vprintf.
    // In a real implementation, we should add vprintf to terminal.c
    printf(fmt);

    printf(KRESET "\n");

    stack_trace();

#ifdef TEST_MODE
    shutdown();
#endif

    hcf();
}

void stack_trace(void)
{
    printf(KBWHT "Stack trace:\n" KRESET);

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
            printf("  [%p] <%s+%p>\n", (void *)stack->rip, symbol, (void *)offset);
        }
        else
        {
            printf("  [%p]\n", (void *)stack->rip);
        }

        stack = stack->rbp;

        // Sanity check to avoid infinite loops or garbage
        if ((uint64_t)stack < 0xFFFF800000000000)
            break;
    }
}
