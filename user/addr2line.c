#include <elf.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static elf64_sym *symtab;
static uint64_t sym_count;
static char *strtab;
static uint64_t strtab_size;

static bool load_symbols(const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f) {
        printf("addr2line: cannot open %s\n", path);
        return false;
    }

    elf64_ehdr ehdr;
    if (fread(&ehdr, sizeof(ehdr), 1, f) != 1)
        goto fail;
    if (*(uint32_t *)ehdr.e_ident != ELF_MAGIC)
        goto fail;
    if (ehdr.e_shoff == 0 || ehdr.e_shnum == 0)
        goto fail;

    uint64_t sh_size = ehdr.e_shnum * sizeof(elf64_shdr);
    elf64_shdr *shdrs = malloc(sh_size);
    if (!shdrs)
        goto fail;

    if (fseek(f, (long)ehdr.e_shoff, SEEK_SET) != 0)
        goto fail_shdrs;
    if (fread(shdrs, sh_size, 1, f) != 1)
        goto fail_shdrs;

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

    sym_count = symtab_sh->sh_size / sizeof(elf64_sym);
    symtab = malloc(symtab_sh->sh_size);
    if (!symtab)
        goto fail_shdrs;
    if (fseek(f, (long)symtab_sh->sh_offset, SEEK_SET) != 0)
        goto fail_sym;
    if (fread(symtab, symtab_sh->sh_size, 1, f) != 1)
        goto fail_sym;

    strtab_size = strtab_sh->sh_size;
    strtab = malloc(strtab_size);
    if (!strtab)
        goto fail_sym;
    if (fseek(f, (long)strtab_sh->sh_offset, SEEK_SET) != 0)
        goto fail_str;
    if (fread(strtab, strtab_size, 1, f) != 1)
        goto fail_str;

    free(shdrs);
    fclose(f);
    return true;

fail_str:
    free(strtab);
fail_sym:
    free(symtab);
fail_shdrs:
    free(shdrs);
fail:
    fclose(f);
    printf("addr2line: failed to load symbols from %s\n", path);
    return false;
}

static void resolve(uint64_t address)
{
    for (uint64_t i = 0; i < sym_count; i++) {
        elf64_sym *sym = &symtab[i];
        if (sym->st_size == 0)
            continue;
        if (address >= sym->st_value && address < sym->st_value + sym->st_size) {
            if (sym->st_name >= strtab_size)
                continue;
            printf("0x%lx: %s+0x%lx\n", address, strtab + sym->st_name, address - sym->st_value);
            return;
        }
    }
    printf("0x%lx: [unknown]\n", address);
}

int main(int argc, char *argv[])
{
    if (argc < 3) {
        printf("usage: addr2line <elf> <addr> [addr...]\n");
        exit();
    }

    if (!load_symbols(argv[1]))
        exit();

    for (int i = 2; i < argc; i++) {
        uint64_t addr = strtoul(argv[i], nullptr, 16);
        resolve(addr);
    }

    free(symtab);
    free(strtab);
    exit();
    return 0;
}
