#pragma once

#include <stddef.h>
#include <stdint.h>

typedef struct ht_str_entry
{
    const char* key;
    const char* value;
} ht_str_entry_t;

typedef struct ht_str_table
{
    ht_str_entry_t* entries;
    size_t capacity;
    size_t count;
    size_t mask;
} ht_str_table_t;

uint32_t ht_str_hash(const char* key);
int ht_str_init(ht_str_table_t* table, ht_str_entry_t* entries, size_t capacity);
int ht_str_insert(ht_str_table_t* table, const char* key, const char* value);
const char* ht_str_get(const ht_str_table_t* table, const char* key);
