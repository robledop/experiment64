#include <elf.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static FILE *elf_file;
static elf64_ehdr ehdr;
static elf64_shdr *shdrs;
static char *shstrtab; // section name string table

static const char *sht_name(uint32_t type)
{
    switch (type) {
    case SHT_NULL:     return "NULL";
    case SHT_PROGBITS: return "PROGBITS";
    case SHT_SYMTAB:   return "SYMTAB";
    case SHT_STRTAB:   return "STRTAB";
    case SHT_RELA:     return "RELA";
    case SHT_HASH:     return "HASH";
    case SHT_DYNAMIC:  return "DYNAMIC";
    case SHT_NOTE:     return "NOTE";
    case SHT_NOBITS:   return "NOBITS";
    case SHT_REL:      return "REL";
    default:           return "UNKNOWN";
    }
}

static void fmt_shflags(uint64_t flags, char *out)
{
    out[0] = (flags & SHF_WRITE)     ? 'W' : '-';
    out[1] = (flags & SHF_ALLOC)     ? 'A' : '-';
    out[2] = (flags & SHF_EXECINSTR) ? 'X' : '-';
    out[3] = '\0';
}

static const char *stb_name(unsigned char info)
{
    switch (ELF64_ST_BIND(info)) {
    case STB_LOCAL:  return "LOCAL";
    case STB_GLOBAL: return "GLOBAL";
    case STB_WEAK:   return "WEAK";
    default:         return "?";
    }
}

static const char *stt_name(unsigned char info)
{
    switch (ELF64_ST_TYPE(info)) {
    case STT_NOTYPE:  return "NOTYPE";
    case STT_OBJECT:  return "OBJECT";
    case STT_FUNC:    return "FUNC";
    case STT_SECTION: return "SECTION";
    case STT_FILE:    return "FILE";
    default:          return "?";
    }
}

static const char *sec_name(uint32_t name_offset)
{
    if (!shstrtab)
        return "";
    return shstrtab + name_offset;
}

static bool load_elf(const char *path)
{
    elf_file = fopen(path, "r");
    if (!elf_file) {
        printf("objdump: cannot open %s\n", path);
        return false;
    }

    if (fread(&ehdr, sizeof(ehdr), 1, elf_file) != 1)
        goto fail;
    if (*(uint32_t *)ehdr.e_ident != ELF_MAGIC) {
        printf("objdump: %s: not an ELF file\n", path);
        goto fail;
    }
    if (ehdr.e_shoff == 0 || ehdr.e_shnum == 0)
        goto fail;

    uint64_t sh_size = ehdr.e_shnum * sizeof(elf64_shdr);
    shdrs = malloc(sh_size);
    if (!shdrs)
        goto fail;
    if (fseek(elf_file, (long)ehdr.e_shoff, SEEK_SET) != 0)
        goto fail;
    if (fread(shdrs, sh_size, 1, elf_file) != 1)
        goto fail;

    // Load section name string table
    if (ehdr.e_shstrndx < ehdr.e_shnum) {
        elf64_shdr *sh = &shdrs[ehdr.e_shstrndx];
        shstrtab = malloc(sh->sh_size);
        if (shstrtab) {
            if (fseek(elf_file, (long)sh->sh_offset, SEEK_SET) != 0 ||
                fread(shstrtab, sh->sh_size, 1, elf_file) != 1) {
                free(shstrtab);
                shstrtab = nullptr;
            }
        }
    }

    return true;

fail:
    if (shdrs) free(shdrs);
    fclose(elf_file);
    return false;
}

static void dump_header(void)
{
    printf("ELF Header:\n");
    printf("  Entry point:       0x%lx\n", ehdr.e_entry);
    printf("  Section headers:   %d (offset 0x%lx)\n", ehdr.e_shnum, ehdr.e_shoff);
    printf("  Program headers:   %d (offset 0x%lx)\n", ehdr.e_phnum, ehdr.e_phoff);
    printf("\n");
}

static void dump_sections(void)
{
    printf("Sections:\n");
    printf("  %-4s %-20s %-10s %-16s %-16s %s\n",
           "Idx", "Name", "Type", "Address", "Size", "Flg");

    for (int i = 0; i < ehdr.e_shnum; i++) {
        elf64_shdr *sh = &shdrs[i];
        char flags[4];
        fmt_shflags(sh->sh_flags, flags);
        printf("  %-4d %-20s %-10s %016lx %016lx %s\n",
               i, sec_name(sh->sh_name), sht_name(sh->sh_type),
               sh->sh_addr, sh->sh_size, flags);
    }
    printf("\n");
}

static void dump_symbols(void)
{
    for (int i = 0; i < ehdr.e_shnum; i++) {
        if (shdrs[i].sh_type != SHT_SYMTAB)
            continue;

        elf64_shdr *sym_sh = &shdrs[i];
        if (sym_sh->sh_link >= ehdr.e_shnum)
            continue;

        elf64_shdr *str_sh = &shdrs[sym_sh->sh_link];
        uint64_t count = sym_sh->sh_size / sizeof(elf64_sym);

        elf64_sym *syms = malloc(sym_sh->sh_size);
        char *strs = malloc(str_sh->sh_size);
        if (!syms || !strs) {
            free(syms);
            free(strs);
            return;
        }

        if (fseek(elf_file, (long)sym_sh->sh_offset, SEEK_SET) != 0 ||
            fread(syms, sym_sh->sh_size, 1, elf_file) != 1)
            goto sym_done;
        if (fseek(elf_file, (long)str_sh->sh_offset, SEEK_SET) != 0 ||
            fread(strs, str_sh->sh_size, 1, elf_file) != 1)
            goto sym_done;

        printf("Symbol table (%lu entries):\n", count);
        printf("  %-16s %-8s %-8s %-8s %s\n",
               "Value", "Size", "Bind", "Type", "Name");

        for (uint64_t j = 0; j < count; j++) {
            elf64_sym *s = &syms[j];
            const char *name = (s->st_name < str_sh->sh_size) ? strs + s->st_name : "";
            printf("  %016lx %-8lu %-8s %-8s %s\n",
                   s->st_value, s->st_size,
                   stb_name(s->st_info), stt_name(s->st_info), name);
        }
        printf("\n");

    sym_done:
        free(syms);
        free(strs);
    }
}

static void dump_hex(const char *section_name)
{
    for (int i = 0; i < ehdr.e_shnum; i++) {
        if (strcmp(sec_name(shdrs[i].sh_name), section_name) != 0)
            continue;

        elf64_shdr *sh = &shdrs[i];
        if (sh->sh_type == SHT_NOBITS) {
            printf("Section %s is NOBITS (no data on disk)\n", section_name);
            return;
        }

        printf("Hex dump of section '%s' (%lu bytes at offset 0x%lx):\n\n",
               section_name, sh->sh_size, sh->sh_offset);

        uint8_t buf[16];
        if (fseek(elf_file, (long)sh->sh_offset, SEEK_SET) != 0)
            return;

        uint64_t addr = sh->sh_addr;
        uint64_t remaining = sh->sh_size;

        while (remaining > 0) {
            uint64_t chunk = remaining > 16 ? 16 : remaining;
            if (fread(buf, chunk, 1, elf_file) != 1)
                break;

            printf("  %08lx  ", addr);

            // Hex bytes
            for (uint64_t j = 0; j < 16; j++) {
                if (j < chunk)
                    printf("%02x ", buf[j]);
                else
                    printf("   ");
                if (j == 7)
                    printf(" ");
            }

            // ASCII
            printf(" |");
            for (uint64_t j = 0; j < chunk; j++) {
                char c = (char)buf[j];
                printf("%c", (c >= 0x20 && c < 0x7f) ? c : '.');
            }
            printf("|\n");

            addr += chunk;
            remaining -= chunk;
        }
        printf("\n");
        return;
    }
    printf("objdump: section '%s' not found\n", section_name);
}

int main(int argc, char *argv[])
{
    if (argc < 3) {
        printf("usage: objdump <option> <elf> [args]\n");
        printf("  -h <elf>            section headers\n");
        printf("  -t <elf>            symbol table\n");
        printf("  -x <elf> <section>  hex dump of section\n");
        exit();
    }

    const char *opt = argv[1];

    if (strcmp(opt, "-x") == 0) {
        if (argc < 4) {
            printf("usage: objdump -x <elf> <section>\n");
            exit();
        }
        if (!load_elf(argv[2]))
            exit();
        dump_hex(argv[3]);
    } else if (strcmp(opt, "-h") == 0 || strcmp(opt, "-t") == 0) {
        if (!load_elf(argv[2]))
            exit();
        if (strcmp(opt, "-h") == 0) {
            dump_header();
            dump_sections();
        } else {
            dump_symbols();
        }
    } else {
        printf("objdump: unknown option %s\n", opt);
    }

    free(shdrs);
    free(shstrtab);
    fclose(elf_file);
    exit();
    return 0;
}
