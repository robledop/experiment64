#pragma once

#include <stdint.h>

typedef struct sym_table sym_table_t;

// Load .symtab/.strtab from an ELF binary on disk.
// Returns NULL if the file has no symbols or can't be read.
sym_table_t *sym_load(const char *elf_path);

// Resolve an address to "function_name+0xNN".
// Returns pointer to static buffer, or NULL if not found.
const char *sym_resolve(const sym_table_t *table, uint64_t address);

// Free loaded symbol table.
void sym_free(sym_table_t *table);
