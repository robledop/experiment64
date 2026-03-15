#pragma once

#include <stdint.h>

#define ELF_MAGIC  0x464C457F

// Section types
#define SHT_NULL     0
#define SHT_PROGBITS 1
#define SHT_SYMTAB   2
#define SHT_STRTAB   3
#define SHT_RELA     4
#define SHT_HASH     5
#define SHT_DYNAMIC  6
#define SHT_NOTE     7
#define SHT_NOBITS   8
#define SHT_REL      9

// Section flags
#define SHF_WRITE     0x1
#define SHF_ALLOC     0x2
#define SHF_EXECINSTR 0x4

// Symbol binding (upper nibble of st_info)
#define STB_LOCAL  0
#define STB_GLOBAL 1
#define STB_WEAK   2

// Symbol type (lower nibble of st_info)
#define STT_NOTYPE  0
#define STT_OBJECT  1
#define STT_FUNC    2
#define STT_SECTION 3
#define STT_FILE    4

#define ELF64_ST_BIND(i) ((i) >> 4)
#define ELF64_ST_TYPE(i) ((i) & 0xf)

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
