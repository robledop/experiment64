#include "symresolve.h"

#include <elf.h>
#include <stdio.h>
#include <stdlib.h>

struct sym_table
{
    elf64_sym *symbols;
    uint64_t sym_count;
    char *strtab;
    uint64_t strtab_size;
};

sym_table_t *sym_load(const char *elf_path)
{
    if (!elf_path)
        return nullptr;

    FILE *f = fopen(elf_path, "r");
    if (!f)
        return nullptr;

    // Read ELF header
    elf64_ehdr ehdr;
    if (fread(&ehdr, sizeof(ehdr), 1, f) != 1)
        goto fail;

    if (*(uint32_t *)ehdr.e_ident != ELF_MAGIC)
        goto fail;

    if (ehdr.e_shoff == 0 || ehdr.e_shnum == 0)
        goto fail;

    // Read section headers
    uint64_t sh_size = ehdr.e_shnum * sizeof(elf64_shdr);
    elf64_shdr *shdrs = malloc(sh_size);
    if (!shdrs)
        goto fail;

    if (fseek(f, (long)ehdr.e_shoff, SEEK_SET) != 0)
        goto fail_shdrs;

    if (fread(shdrs, sh_size, 1, f) != 1)
        goto fail_shdrs;

    // Find SHT_SYMTAB
    elf64_shdr *symtab_sh = nullptr;
    for (int i = 0; i < ehdr.e_shnum; i++) {
        if (shdrs[i].sh_type == SHT_SYMTAB) {
            symtab_sh = &shdrs[i];
            break;
        }
    }

    if (!symtab_sh || symtab_sh->sh_link >= ehdr.e_shnum)
        goto fail_shdrs;

    elf64_shdr *strtab_sh = &shdrs[symtab_sh->sh_link];
    if (strtab_sh->sh_type != SHT_STRTAB)
        goto fail_shdrs;

    // Load symbol table
    uint64_t sym_count = symtab_sh->sh_size / sizeof(elf64_sym);
    elf64_sym *symbols = malloc(symtab_sh->sh_size);
    if (!symbols)
        goto fail_shdrs;

    if (fseek(f, (long)symtab_sh->sh_offset, SEEK_SET) != 0)
        goto fail_syms;

    if (fread(symbols, symtab_sh->sh_size, 1, f) != 1)
        goto fail_syms;

    // Load string table
    char *strtab = malloc(strtab_sh->sh_size);
    if (!strtab)
        goto fail_syms;

    if (fseek(f, (long)strtab_sh->sh_offset, SEEK_SET) != 0)
        goto fail_strtab;

    if (fread(strtab, strtab_sh->sh_size, 1, f) != 1)
        goto fail_strtab;

    free(shdrs);
    fclose(f);

    sym_table_t *table = malloc(sizeof(sym_table_t));
    if (!table) {
        free(symbols);
        free(strtab);
        return nullptr;
    }

    table->symbols = symbols;
    table->sym_count = sym_count;
    table->strtab = strtab;
    table->strtab_size = strtab_sh->sh_size;
    return table;

fail_strtab:
    free(strtab);
fail_syms:
    free(symbols);
fail_shdrs:
    free(shdrs);
fail:
    fclose(f);
    return nullptr;
}

const char *sym_resolve(const sym_table_t *table, uint64_t address)
{
    static char buf[128];

    if (!table || !table->symbols || !table->strtab)
        return nullptr;

    for (uint64_t i = 0; i < table->sym_count; i++) {
        elf64_sym *sym = &table->symbols[i];
        if (sym->st_size == 0)
            continue;
        if (address >= sym->st_value && address < sym->st_value + sym->st_size) {
            if (sym->st_name >= table->strtab_size)
                continue;
            uint64_t offset = address - sym->st_value;
            snprintf(buf, sizeof(buf), "%s+0x%lx", table->strtab + sym->st_name, offset);
            return buf;
        }
    }
    return nullptr;
}

void sym_free(sym_table_t *table)
{
    if (!table)
        return;
    free(table->symbols);
    free(table->strtab);
    free(table);
}
