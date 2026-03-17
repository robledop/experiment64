#pragma once

#include <stdint.h>
#include <stddef.h>

/* ── ELF types (self-contained, no libc dependency) ────────────────────── */

typedef uint64_t elf64_addr;
typedef uint64_t elf64_off;
typedef uint16_t elf64_half;
typedef uint32_t elf64_word;
typedef int32_t  elf64_sword;
typedef uint64_t elf64_xword;
typedef int64_t  elf64_sxword;

/** @brief ELF file header (64-bit). */
typedef struct
{
    unsigned char e_ident[16];
    elf64_half e_type;
    elf64_half e_machine;
    elf64_word e_version;
    elf64_addr e_entry;
    elf64_off  e_phoff;
    elf64_off  e_shoff;
    elf64_word e_flags;
    elf64_half e_ehsize;
    elf64_half e_phentsize;
    elf64_half e_phnum;
    elf64_half e_shentsize;
    elf64_half e_shnum;
    elf64_half e_shstrndx;
} rtld_elf64_ehdr;

/** @brief ELF program header (64-bit). */
typedef struct
{
    elf64_word  p_type;
    elf64_word  p_flags;
    elf64_off   p_offset;
    elf64_addr  p_vaddr;
    elf64_addr  p_paddr;
    elf64_xword p_filesz;
    elf64_xword p_memsz;
    elf64_xword p_align;
} rtld_Elf64_Phdr;

/** @brief ELF dynamic section entry. */
typedef struct
{
    elf64_sxword d_tag;
    union
    {
        elf64_xword d_val;
        elf64_addr  d_ptr;
    } d_un;
} rtld_Elf64_Dyn;

/** @brief ELF relocation entry with explicit addend (RELA). */
typedef struct
{
    elf64_addr   r_offset;
    elf64_xword  r_info;
    elf64_sxword r_addend;
} rtld_Elf64_Rela;

/** @brief ELF symbol table entry (64-bit). */
typedef struct
{
    elf64_word    st_name;
    unsigned char st_info;
    unsigned char st_other;
    elf64_half    st_shndx;
    elf64_addr    st_value;
    elf64_xword   st_size;
} rtld_elf64_sym;

/* ELF constants */
#define RTLD_ELF_MAGIC   0x464C457F
#define RTLD_PT_NULL     0
#define RTLD_PT_LOAD     1
#define RTLD_PT_DYNAMIC  2
#define RTLD_PT_INTERP   3
#define RTLD_PT_PHDR     6

#define RTLD_DT_NULL     0
#define RTLD_DT_NEEDED   1
#define RTLD_DT_PLTRELSZ 2
#define RTLD_DT_PLTGOT   3
#define RTLD_DT_HASH     4
#define RTLD_DT_STRTAB   5
#define RTLD_DT_SYMTAB   6
#define RTLD_DT_RELA     7
#define RTLD_DT_RELASZ   8
#define RTLD_DT_RELAENT  9
#define RTLD_DT_STRSZ    10
#define RTLD_DT_SONAME   14
#define RTLD_DT_PLTREL   20
#define RTLD_DT_JMPREL   23

#define RTLD_R_X86_64_NONE      0
#define RTLD_R_X86_64_64        1
#define RTLD_R_X86_64_COPY      5
#define RTLD_R_X86_64_GLOB_DAT  6
#define RTLD_R_X86_64_JUMP_SLOT 7
#define RTLD_R_X86_64_RELATIVE  8
#define RTLD_R_X86_64_TPOFF64  18

#define RTLD_ELF64_R_SYM(i)  ((i) >> 32)
#define RTLD_ELF64_R_TYPE(i) ((i) & 0xFFFFFFFF)

#define RTLD_STB_LOCAL  0
#define RTLD_STB_GLOBAL 1
#define RTLD_STB_WEAK   2

#define RTLD_ELF64_ST_BIND(i) ((i) >> 4)

#define RTLD_SHN_UNDEF 0

/* Auxiliary vector types */
#define RTLD_AT_NULL   0
#define RTLD_AT_PHDR   3
#define RTLD_AT_PHENT  4
#define RTLD_AT_PHNUM  5
#define RTLD_AT_PAGESZ 6
#define RTLD_AT_BASE   7
#define RTLD_AT_ENTRY  9

/* Syscall numbers */
#define RTLD_SYS_WRITE  0
#define RTLD_SYS_READ   1
#define RTLD_SYS_EXIT   3
#define RTLD_SYS_OPEN   10
#define RTLD_SYS_CLOSE  11
#define RTLD_SYS_MMAP   17

/* mmap flags */
#define RTLD_PROT_READ  0x1
#define RTLD_PROT_WRITE 0x2
#define RTLD_PROT_EXEC  0x4
#define RTLD_MAP_SHARED    0x01
#define RTLD_MAP_PRIVATE   0x02
#define RTLD_MAP_ANONYMOUS 0x20

#define RTLD_PAGE_SIZE 4096

/* ── DSO (Dynamic Shared Object) descriptor ────────────────────────────── */

/** @brief Maximum number of shared libraries that can be loaded. */
#define RTLD_MAX_DSO 16

/**
 * @brief Represents a loaded dynamic shared object (executable or library).
 *
 * Each loaded ELF image (the main executable and each shared library) gets a
 * dso_t. These are kept in a singly-linked list in load order, which determines
 * symbol resolution priority.
 */
typedef struct dso
{
    const char *name;           /**< SONAME or filesystem path of this object */
    uint64_t base;              /**< Load bias (0 for the main executable) */
    rtld_Elf64_Dyn *dynamic;    /**< Pointer to the PT_DYNAMIC segment in memory */
    rtld_elf64_sym *symtab;     /**< .dynsym section: dynamic symbol table */
    const char *strtab;         /**< .dynstr section: dynamic string table */
    uint32_t *hash;             /**< DT_HASH table (SysV format) */
    rtld_Elf64_Rela *rela;      /**< DT_RELA relocations (non-PLT) */
    size_t rela_count;          /**< Number of DT_RELA entries */
    rtld_Elf64_Rela *jmprel;    /**< DT_JMPREL relocations (PLT / jump slots) */
    size_t jmprel_count;        /**< Number of DT_JMPREL entries */
    uint32_t nsyms;             /**< Symbol count from DT_HASH nchain field */
    struct dso *next;           /**< Next DSO in load order */
} dso_t;

/**
 * @brief Parsed auxiliary vector values.
 */
typedef struct
{
    uint64_t at_phdr;   /**< Program header table address */
    uint64_t at_phent;  /**< Size of one program header entry */
    uint64_t at_phnum;  /**< Number of program header entries */
    uint64_t at_pagesz; /**< System page size */
    uint64_t at_base;   /**< Interpreter base address (ld.so load bias) */
    uint64_t at_entry;  /**< Program entry point */
} rtld_auxv_t;

/* ── Syscall wrappers (defined in rtld_syscall.S) ──────────────────────── */

/**
 * @brief Perform a raw syscall with the given number and arguments.
 *
 * These are implemented in assembly with no libc dependency.
 * Hidden visibility ensures PC-relative calls (no PLT/GOT), which is
 * critical because ld.so must call these before self-relocation.
 */
__attribute__((visibility("hidden"))) long rtld_syscall0(long n);
__attribute__((visibility("hidden"))) long rtld_syscall1(long n, long a1);
__attribute__((visibility("hidden"))) long rtld_syscall2(long n, long a1, long a2);
__attribute__((visibility("hidden"))) long rtld_syscall3(long n, long a1, long a2, long a3);
__attribute__((visibility("hidden"))) long rtld_syscall6(long n, long a1, long a2, long a3, long a4, long a5, long a6);

/* ── Minimal libc replacements ─────────────────────────────────────────── */

/** @brief Write a string to stdout (fd 1). */
void rtld_puts(const char *s);

/** @brief Write a message and abort. */
__attribute__((noreturn)) void rtld_die(const char *msg);

/** @brief Entry point called from rtld_start.S. Returns the app entry address. */
uint64_t rtld_main(uint64_t *sp);
