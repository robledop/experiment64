#pragma once

#include <elf.h>
#include <stdint.h>
#include <stdio.h>

/* ── ELF file handle ───────────────────────────────────────────────────── */

/**
 * @brief Opaque handle to an opened ELF file with parsed headers.
 *
 * Created by elf_file_open(). Provides access to the ELF header,
 * program headers, section headers, and section name string table.
 * The underlying FILE* remains open until elf_file_close().
 */
typedef struct elf_file elf_file_t;

/**
 * @brief Open an ELF file and parse its headers.
 *
 * Reads and validates the ELF header, then loads program headers,
 * section headers, and the section name string table.
 *
 * @param path Filesystem path to the ELF binary.
 * @return Handle on success, or nullptr on failure.
 */
elf_file_t *elf_file_open(const char *path);

/**
 * @brief Close an ELF file handle and free all associated memory.
 * @param ef Handle returned by elf_file_open().
 */
void elf_file_close(elf_file_t *ef);

/**
 * @brief Get the ELF file header.
 * @param ef Handle returned by elf_file_open().
 * @return Pointer to the ELF header (valid until elf_file_close).
 */
const elf64_ehdr *elf_file_header(const elf_file_t *ef);

/**
 * @brief Get the program header array.
 * @param ef    Handle returned by elf_file_open().
 * @param count Output: number of program headers (may be nullptr).
 * @return Pointer to the program header array, or nullptr if none.
 */
const Elf64_Phdr *elf_file_phdrs(const elf_file_t *ef, int *count);

/**
 * @brief Get the section header array.
 * @param ef    Handle returned by elf_file_open().
 * @param count Output: number of section headers (may be nullptr).
 * @return Pointer to the section header array, or nullptr if none.
 */
const elf64_shdr *elf_file_shdrs(const elf_file_t *ef, int *count);

/**
 * @brief Get a section's name from the section name string table.
 * @param ef Handle returned by elf_file_open().
 * @param sh Section header whose name to look up.
 * @return Section name string, or "" if unavailable.
 */
const char *elf_file_section_name(const elf_file_t *ef, const elf64_shdr *sh);

/**
 * @brief Read raw data from the ELF file at a given offset.
 * @param ef     Handle returned by elf_file_open().
 * @param offset File offset to read from.
 * @param buf    Destination buffer.
 * @param size   Number of bytes to read.
 * @return true on success, false on failure.
 */
bool elf_file_read_at(elf_file_t *ef, uint64_t offset, void *buf, size_t size);

/**
 * @brief Convert a virtual address to a file offset using PT_LOAD segments.
 * @param ef    Handle returned by elf_file_open().
 * @param vaddr Virtual address to convert.
 * @return File offset, or 0 if the address is not in any PT_LOAD segment.
 */
uint64_t elf_file_vaddr_to_offset(const elf_file_t *ef, uint64_t vaddr);

/**
 * @brief Get the underlying FILE* for direct reads (e.g. hex dump).
 * @param ef Handle returned by elf_file_open().
 * @return The FILE* (valid until elf_file_close).
 */
FILE *elf_file_fp(elf_file_t *ef);

/* ── Symbol resolution ─────────────────────────────────────────────────── */

/** @brief Opaque symbol table handle. */
typedef struct sym_table sym_table_t;

/**
 * @brief Load the symbol table from an already-opened ELF file.
 * @param ef Handle returned by elf_file_open().
 * @return Symbol table on success, or nullptr if no symbols.
 */
sym_table_t *sym_load_from(elf_file_t *ef);

/**
 * @brief Load the symbol table from an ELF file by path (convenience).
 * @param elf_path Filesystem path to the ELF binary.
 * @return Symbol table on success, or nullptr if no symbols.
 */
sym_table_t *sym_load(const char *elf_path);

/**
 * @brief Resolve an address to "function_name+0xNN".
 * @param table Symbol table from sym_load() or sym_load_from().
 * @param address Virtual address to resolve.
 * @return Pointer to a static buffer with the result, or nullptr if not found.
 */
const char *sym_resolve(const sym_table_t *table, uint64_t address);

/**
 * @brief Free a loaded symbol table.
 * @param table Symbol table to free.
 */
void sym_free(sym_table_t *table);
