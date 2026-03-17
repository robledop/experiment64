#pragma once

#include <stdint.h>
#include <mem/vmm.h>

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

/** @brief ELF object file types. */
#define ET_EXEC 2
#define ET_DYN  3

/** @brief Special section header index: symbol is undefined. */
#define SHN_UNDEF 0

/* ── Dynamic linking structures ────────────────────────────────────────── */

/**
 * @brief ELF dynamic section entry.
 *
 * The PT_DYNAMIC segment contains an array of these, terminated by DT_NULL.
 * Each entry describes a property of the dynamic object (needed libraries,
 * symbol table location, relocation tables, etc.).
 */
typedef struct
{
    elf64_sxword d_tag; /**< Entry type (DT_*) */
    union
    {
        elf64_xword d_val; /**< Integer value */
        elf64_addr d_ptr;  /**< Virtual address */
    } d_un;
} Elf64_Dyn;

/**
 * @brief ELF relocation entry with explicit addend (RELA).
 *
 * Used by x86_64 for all relocations. The linker/loader patches the location
 * at r_offset based on the relocation type encoded in r_info.
 */
typedef struct
{
    elf64_addr  r_offset;  /**< Virtual address of the relocation target */
    elf64_xword r_info;    /**< Symbol index and relocation type */
    elf64_sxword r_addend; /**< Constant addend for the relocation */
} Elf64_Rela;

/** @brief Extract symbol table index from r_info. */
#define ELF64_R_SYM(i)  ((i) >> 32)
/** @brief Extract relocation type from r_info. */
#define ELF64_R_TYPE(i) ((i) & 0xFFFFFFFF)

/* ── Dynamic tags (d_tag values) ───────────────────────────────────────── */

#define DT_NULL     0   /**< End of dynamic section */
#define DT_NEEDED   1   /**< Name of a needed shared library (d_val = strtab offset) */
#define DT_HASH     4   /**< Address of SysV symbol hash table */
#define DT_STRTAB   5   /**< Address of the dynamic string table */
#define DT_SYMTAB   6   /**< Address of the dynamic symbol table */
#define DT_RELA     7   /**< Address of RELA relocation table */
#define DT_RELASZ   8   /**< Total size of RELA table in bytes */
#define DT_RELAENT  9   /**< Size of one RELA entry */
#define DT_STRSZ    10  /**< Size of the string table in bytes */
#define DT_PLTGOT   3   /**< Address of the PLT/GOT */
#define DT_PLTRELSZ 2   /**< Total size of PLT relocation entries */
#define DT_JMPREL   23  /**< Address of PLT relocation table */
#define DT_PLTREL   20  /**< Type of PLT relocations (DT_RELA) */
#define DT_SONAME   14  /**< Shared object name (d_val = strtab offset) */

/* ── x86_64 relocation types ──────────────────────────────────────────── */

#define R_X86_64_NONE      0  /**< No relocation */
#define R_X86_64_64        1  /**< Direct 64-bit: S + A */
#define R_X86_64_GLOB_DAT  6  /**< GOT entry: S */
#define R_X86_64_JUMP_SLOT 7  /**< PLT entry: S */
#define R_X86_64_RELATIVE  8  /**< Relative: B + A */
#define R_X86_64_COPY      5  /**< Copy symbol from shared object */

/* ── Auxiliary vector types ────────────────────────────────────────────── */

/** @brief Auxiliary vector entry, passed on the initial stack by the kernel. */
typedef struct
{
    uint64_t a_type; /**< Entry type (AT_*) */
    uint64_t a_val;  /**< Entry value */
} Elf64_auxv_t;

#define AT_NULL   0  /**< End of auxiliary vector */
#define AT_PHDR   3  /**< Program header table address */
#define AT_PHENT  4  /**< Size of one program header entry */
#define AT_PHNUM  5  /**< Number of program header entries */
#define AT_PAGESZ 6  /**< System page size */
#define AT_BASE   7  /**< Interpreter base address */
#define AT_ENTRY  9  /**< Program entry point */

/* ── ELF loader result ─────────────────────────────────────────────────── */

/**
 * @brief Result of loading an ELF executable, including dynamic linking metadata.
 *
 * Populated by elf_load_ex(). For statically linked binaries, interp_base and
 * interp_entry are zero and interp is empty.
 */
typedef struct
{
    uint64_t entry;        /**< Executable's entry point (e_entry) */
    uint64_t max_vaddr;    /**< Highest virtual address mapped by PT_LOAD segments */
    uint64_t phdr_vaddr;   /**< Virtual address of the program header table in memory */
    uint16_t phent;        /**< Size of each program header entry (e_phentsize) */
    uint16_t phnum;        /**< Number of program header entries (e_phnum) */
    uint64_t interp_base;  /**< Load bias applied to the interpreter (0 if static) */
    uint64_t interp_entry; /**< Entry point of the interpreter (0 if static) */
    char interp[128];      /**< PT_INTERP path string, empty if statically linked */
} elf_load_result_t;

/**
 * @brief Load an ELF executable into an address space (legacy interface).
 *
 * @param path        Absolute path to the ELF binary.
 * @param entry_point Output: the program entry point.
 * @param max_vaddr   Output: highest virtual address mapped.
 * @param pml4        Page table root to map segments into.
 * @return true on success, false on failure.
 */
bool elf_load(const char* path, uint64_t* entry_point, uint64_t* max_vaddr, pml4_t pml4);

/**
 * @brief Load an ELF executable with full dynamic linking support.
 *
 * Loads all PT_LOAD segments into the given address space. If a PT_INTERP
 * segment is present, also loads the dynamic linker at a high bias address
 * and records its entry point in @p result.
 *
 * @param path   Absolute path to the ELF binary.
 * @param result Output structure filled with load metadata and interpreter info.
 * @param pml4   Page table root to map segments into.
 * @return true on success, false on failure.
 */
bool elf_load_ex(const char* path, elf_load_result_t* result, pml4_t pml4);
