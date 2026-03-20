#include <symresolve.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

static void dump_header(elf_file_t *ef)
{
    const elf64_ehdr *ehdr = elf_file_header(ef);
    printf("ELF Header:\n");
    printf("  Entry point:       0x%lx\n", ehdr->e_entry);
    printf("  Section headers:   %d (offset 0x%lx)\n", ehdr->e_shnum, ehdr->e_shoff);
    printf("  Program headers:   %d (offset 0x%lx)\n", ehdr->e_phnum, ehdr->e_phoff);
    printf("\n");
}

static void dump_sections(elf_file_t *ef)
{
    int count = 0;
    const elf64_shdr *shdrs = elf_file_shdrs(ef, &count);
    if (!shdrs) return;

    printf("Sections:\n");
    printf("  %-4s %-20s %-10s %-16s %-16s %s\n",
           "Idx", "Name", "Type", "Address", "Size", "Flg");

    for (int i = 0; i < count; i++) {
        const elf64_shdr *sh = &shdrs[i];
        char flags[4];
        fmt_shflags(sh->sh_flags, flags);
        printf("  %-4d %-20s %-10s %016lx %016lx %s\n",
               i, elf_file_section_name(ef, sh), sht_name(sh->sh_type),
               sh->sh_addr, sh->sh_size, flags);
    }
    printf("\n");
}

static void dump_symbols(elf_file_t *ef)
{
    int sh_count = 0;
    const elf64_shdr *shdrs = elf_file_shdrs(ef, &sh_count);
    if (!shdrs) return;

    for (int i = 0; i < sh_count; i++) {
        if (shdrs[i].sh_type != SHT_SYMTAB)
            continue;

        const elf64_shdr *sym_sh = &shdrs[i];
        if (sym_sh->sh_link >= (uint32_t)sh_count)
            continue;

        const elf64_shdr *str_sh = &shdrs[sym_sh->sh_link];
        uint64_t count = sym_sh->sh_size / sizeof(elf64_sym);

        elf64_sym *syms = malloc(sym_sh->sh_size);
        char *strs = malloc(str_sh->sh_size);
        if (!syms || !strs) {
            free(syms);
            free(strs);
            return;
        }

        if (!elf_file_read_at(ef, sym_sh->sh_offset, syms, sym_sh->sh_size) ||
            !elf_file_read_at(ef, str_sh->sh_offset, strs, str_sh->sh_size))
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

static void dump_hex(elf_file_t *ef, const char *section_name)
{
    int sh_count = 0;
    const elf64_shdr *shdrs = elf_file_shdrs(ef, &sh_count);
    if (!shdrs) return;

    for (int i = 0; i < sh_count; i++) {
        if (strcmp(elf_file_section_name(ef, &shdrs[i]), section_name) != 0)
            continue;

        const elf64_shdr *sh = &shdrs[i];
        if (sh->sh_type == SHT_NOBITS) {
            printf("Section %s is NOBITS (no data on disk)\n", section_name);
            return;
        }

        printf("Hex dump of section '%s' (%lu bytes at offset 0x%lx):\n\n",
               section_name, sh->sh_size, sh->sh_offset);

        FILE *fp = elf_file_fp(ef);
        if (fseek(fp, (long)sh->sh_offset, SEEK_SET) != 0)
            return;

        uint8_t buf[16];
        uint64_t addr = sh->sh_addr;
        uint64_t remaining = sh->sh_size;

        while (remaining > 0) {
            uint64_t chunk = remaining > 16 ? 16 : remaining;
            if (fread(buf, chunk, 1, fp) != 1)
                break;

            printf("  %08lx  ", addr);

            for (uint64_t j = 0; j < 16; j++) {
                if (j < chunk)
                    printf("%02x ", buf[j]);
                else
                    printf("   ");
                if (j == 7)
                    printf(" ");
            }

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
        elf_file_t *ef = elf_file_open(argv[2]);
        if (!ef) { printf("objdump: cannot open %s\n", argv[2]); exit(); }
        dump_hex(ef, argv[3]);
        elf_file_close(ef);
    } else if (strcmp(opt, "-h") == 0 || strcmp(opt, "-t") == 0) {
        elf_file_t *ef = elf_file_open(argv[2]);
        if (!ef) { printf("objdump: cannot open %s\n", argv[2]); exit(); }
        if (strcmp(opt, "-h") == 0) {
            dump_header(ef);
            dump_sections(ef);
        } else {
            dump_symbols(ef);
        }
        elf_file_close(ef);
    } else {
        printf("objdump: unknown option %s\n", opt);
    }

    exit();
}
