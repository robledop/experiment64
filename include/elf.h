#pragma once

#include <stdint.h>
#include <vmm.h>

#define SHT_SYMTAB 2
#define SHT_STRTAB 3
#define ELF_MAGIC 0x464C457F

#define PT_nullptr 0
#define PT_LOAD 1
#define PT_DYNAMIC 2
#define PT_INTERP 3
#define PT_NOTE 4
#define PT_SHLIB 5
#define PT_PHDR 6

#define PF_X 0x1
#define PF_W 0x2
#define PF_R 0x4

typedef uint64_t elf64_addr;
typedef uint64_t elf64_off;
typedef uint16_t elf64_half;
typedef uint32_t elf64_word;
typedef int32_t elf64_sword;
typedef uint64_t elf64_xword;
typedef int64_t elf64_sxword;

typedef struct
{
    unsigned char e_ident[16]; /* ELF identification */
    elf64_half e_type; /* Object file type */
    elf64_half e_machine; /* Machine type */
    elf64_word e_version; /* Object file version */
    elf64_addr e_entry; /* Entry point address */
    elf64_off e_phoff; /* Program header offset */
    elf64_off e_shoff; /* Section header offset */
    elf64_word e_flags; /* Processor-specific flags */
    elf64_half e_ehsize; /* ELF header size */
    elf64_half e_phentsize; /* Program header entry size */
    elf64_half e_phnum; /* Number of program header entries */
    elf64_half e_shentsize; /* Section header entry size */
    elf64_half e_shnum; /* Number of section header entries */
    elf64_half e_shstrndx; /* Section name string table index */
} elf64_ehdr;


typedef struct
{
    elf64_word p_type; /* Type of segment */
    elf64_word p_flags; /* Segment attributes */
    elf64_off p_offset; /* Offset in file */
    elf64_addr p_vaddr; /* Virtual address in memory */
    elf64_addr p_paddr; /* Physical address in memory */
    elf64_xword p_filesz; /* Size of segment in file */
    elf64_xword p_memsz; /* Size of segment in memory */
    elf64_xword p_align; /* Alignment of segment */
} Elf64_Phdr;

typedef struct
{
    elf64_word sh_name; /* Section name */
    elf64_word sh_type; /* Section type */
    elf64_xword sh_flags; /* Section attributes */
    elf64_addr sh_addr; /* Virtual address in memory */
    elf64_off sh_offset; /* Offset in file */
    elf64_xword sh_size; /* Size of section */
    elf64_word sh_link; /* Link to other section */
    elf64_word sh_info; /* Miscellaneous information */
    elf64_xword sh_addralign; /* Address alignment boundary */
    elf64_xword sh_entsize; /* Size of entries, if section has table */
} Elf64_Shdr;

typedef struct
{
    elf64_word st_name; /* Symbol name */
    unsigned char st_info; /* Type and Binding attributes */
    unsigned char st_other; /* Reserved */
    elf64_half st_shndx; /* Section table index */
    elf64_addr st_value; /* Symbol value */
    elf64_xword st_size; /* Size of object (e.g., common) */
} elf64_sym;


bool elf_load(const char* path, uint64_t* entry_point, uint64_t* max_vaddr, pml4_t pml4);
