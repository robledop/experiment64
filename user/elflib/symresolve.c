#include <symresolve.h>

#include <elf.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── elf_file_t implementation ─────────────────────────────────────────── */

struct elf_file
{
    FILE *fp;
    elf64_ehdr ehdr;
    Elf64_Phdr *phdrs;    /**< Program headers (nullptr if e_phnum == 0) */
    elf64_shdr *shdrs;    /**< Section headers (nullptr if e_shnum == 0) */
    char *shstrtab;       /**< Section name string table */
    uint64_t shstrtab_sz; /**< Size of shstrtab */
};

elf_file_t *elf_file_open(const char *path)
{
    if (!path)
        return nullptr;

    FILE *fp = fopen(path, "r");
    if (!fp)
        return nullptr;

    elf64_ehdr ehdr;
    if (fread(&ehdr, sizeof(ehdr), 1, fp) != 1)
        goto fail_fp;
    if (*(uint32_t *)ehdr.e_ident != ELF_MAGIC)
        goto fail_fp;

    elf_file_t *ef = malloc(sizeof(elf_file_t));
    if (!ef)
        goto fail_fp;

    memset(ef, 0, sizeof(*ef));
    ef->fp = fp;
    ef->ehdr = ehdr;

    /* Load program headers */
    if (ehdr.e_phoff && ehdr.e_phnum) {
        size_t ph_size = ehdr.e_phnum * ehdr.e_phentsize;
        ef->phdrs = malloc(ph_size);
        if (ef->phdrs) {
            if (fseek(fp, (long)ehdr.e_phoff, SEEK_SET) != 0 ||
                fread(ef->phdrs, ph_size, 1, fp) != 1)
            {
                free(ef->phdrs);
                ef->phdrs = nullptr;
            }
        }
    }

    /* Load section headers */
    if (ehdr.e_shoff && ehdr.e_shnum) {
        size_t sh_size = ehdr.e_shnum * sizeof(elf64_shdr);
        ef->shdrs = malloc(sh_size);
        if (ef->shdrs) {
            if (fseek(fp, (long)ehdr.e_shoff, SEEK_SET) != 0 ||
                fread(ef->shdrs, sh_size, 1, fp) != 1)
            {
                free(ef->shdrs);
                ef->shdrs = nullptr;
            }
        }
    }

    /* Load section name string table */
    if (ef->shdrs && ehdr.e_shstrndx < ehdr.e_shnum) {
        elf64_shdr *sh = &ef->shdrs[ehdr.e_shstrndx];
        ef->shstrtab = malloc(sh->sh_size);
        ef->shstrtab_sz = sh->sh_size;
        if (ef->shstrtab) {
            if (fseek(fp, (long)sh->sh_offset, SEEK_SET) != 0 ||
                fread(ef->shstrtab, sh->sh_size, 1, fp) != 1)
            {
                free(ef->shstrtab);
                ef->shstrtab = nullptr;
                ef->shstrtab_sz = 0;
            }
        }
    }

    return ef;

fail_fp:
    fclose(fp);
    return nullptr;
}

void elf_file_close(elf_file_t *ef)
{
    if (!ef) return;
    free(ef->phdrs);
    free(ef->shdrs);
    free(ef->shstrtab);
    fclose(ef->fp);
    free(ef);
}

const elf64_ehdr *elf_file_header(const elf_file_t *ef)
{
    return ef ? &ef->ehdr : nullptr;
}

const Elf64_Phdr *elf_file_phdrs(const elf_file_t *ef, int *count)
{
    if (count) *count = ef ? ef->ehdr.e_phnum : 0;
    return ef ? ef->phdrs : nullptr;
}

const elf64_shdr *elf_file_shdrs(const elf_file_t *ef, int *count)
{
    if (count) *count = ef ? ef->ehdr.e_shnum : 0;
    return ef ? ef->shdrs : nullptr;
}

const char *elf_file_section_name(const elf_file_t *ef, const elf64_shdr *sh)
{
    if (!ef || !ef->shstrtab || !sh || sh->sh_name >= ef->shstrtab_sz)
        return "";
    return ef->shstrtab + sh->sh_name;
}

bool elf_file_read_at(elf_file_t *ef, uint64_t offset, void *buf, size_t size)
{
    if (!ef || !buf) return false;
    if (fseek(ef->fp, (long)offset, SEEK_SET) != 0) return false;
    return fread(buf, size, 1, ef->fp) == 1;
}

uint64_t elf_file_vaddr_to_offset(const elf_file_t *ef, uint64_t vaddr)
{
    if (!ef || !ef->phdrs) return 0;
    for (int i = 0; i < ef->ehdr.e_phnum; i++) {
        if (ef->phdrs[i].p_type != PT_LOAD) continue;
        if (vaddr >= ef->phdrs[i].p_vaddr &&
            vaddr < ef->phdrs[i].p_vaddr + ef->phdrs[i].p_filesz)
        {
            return ef->phdrs[i].p_offset + (vaddr - ef->phdrs[i].p_vaddr);
        }
    }
    return 0;
}

FILE *elf_file_fp(elf_file_t *ef)
{
    return ef ? ef->fp : nullptr;
}

/* ── Symbol table implementation ───────────────────────────────────────── */

struct sym_table
{
    elf64_sym *symbols;
    uint64_t sym_count;
    char *strtab;
    uint64_t strtab_size;
};

sym_table_t *sym_load_from(elf_file_t *ef)
{
    if (!ef || !ef->shdrs)
        return nullptr;

    /* Find SHT_SYMTAB */
    elf64_shdr *symtab_sh = nullptr;
    for (int i = 0; i < ef->ehdr.e_shnum; i++) {
        if (ef->shdrs[i].sh_type == SHT_SYMTAB) {
            symtab_sh = &ef->shdrs[i];
            break;
        }
    }

    if (!symtab_sh || symtab_sh->sh_link >= ef->ehdr.e_shnum)
        return nullptr;

    elf64_shdr *strtab_sh = &ef->shdrs[symtab_sh->sh_link];
    if (strtab_sh->sh_type != SHT_STRTAB)
        return nullptr;

    uint64_t sym_count = symtab_sh->sh_size / sizeof(elf64_sym);
    elf64_sym *symbols = malloc(symtab_sh->sh_size);
    if (!symbols)
        return nullptr;

    if (!elf_file_read_at(ef, symtab_sh->sh_offset, symbols, symtab_sh->sh_size)) {
        free(symbols);
        return nullptr;
    }

    char *strtab = malloc(strtab_sh->sh_size);
    if (!strtab) {
        free(symbols);
        return nullptr;
    }

    if (!elf_file_read_at(ef, strtab_sh->sh_offset, strtab, strtab_sh->sh_size)) {
        free(strtab);
        free(symbols);
        return nullptr;
    }

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
}

sym_table_t *sym_load(const char *elf_path)
{
    elf_file_t *ef = elf_file_open(elf_path);
    if (!ef) return nullptr;

    sym_table_t *table = sym_load_from(ef);
    elf_file_close(ef);
    return table;
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
