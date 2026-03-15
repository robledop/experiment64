#pragma once

#include <stdint.h>

#define ELF_MAGIC  0x464C457F
#define SHT_SYMTAB 2
#define SHT_STRTAB 3

typedef uint64_t elf64_addr;
typedef uint64_t elf64_off;
typedef uint16_t elf64_half;
typedef uint32_t elf64_word;
typedef int32_t  elf64_sword;
typedef uint64_t elf64_xword;
typedef int64_t  elf64_sxword;

typedef struct
{
    unsigned char e_ident[16];
    elf64_half e_type;
    elf64_half e_machine;
    elf64_word e_version;
    elf64_addr e_entry;
    elf64_off e_phoff;
    elf64_off e_shoff;
    elf64_word e_flags;
    elf64_half e_ehsize;
    elf64_half e_phentsize;
    elf64_half e_phnum;
    elf64_half e_shentsize;
    elf64_half e_shnum;
    elf64_half e_shstrndx;
} elf64_ehdr;

typedef struct
{
    elf64_word sh_name;
    elf64_word sh_type;
    elf64_xword sh_flags;
    elf64_addr sh_addr;
    elf64_off sh_offset;
    elf64_xword sh_size;
    elf64_word sh_link;
    elf64_word sh_info;
    elf64_xword sh_addralign;
    elf64_xword sh_entsize;
} elf64_shdr;

typedef struct
{
    elf64_word st_name;
    unsigned char st_info;
    unsigned char st_other;
    elf64_half st_shndx;
    elf64_addr st_value;
    elf64_xword st_size;
} elf64_sym;
