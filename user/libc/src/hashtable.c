#include <hashtable.h>
#include <string.h>

static int ht_is_power_of_two(const size_t value)
{
    return value != 0 && ((value & (value - 1)) == 0);
}

uint32_t ht_str_hash(const char* key)
{
    uint32_t hash = 2166136261u;
    for (auto p = (const unsigned char*)key; *p; ++p)
    {
        hash ^= (uint32_t)(*p);
        hash *= 16777619u;
    }
    return hash;
}

int ht_str_init(ht_str_table_t* table, ht_str_entry_t* entries, const size_t capacity)
{
    if (!table || !entries || capacity == 0)
        return -1;

    table->entries = entries;
    table->capacity = capacity;
    table->count = 0;
    table->mask = ht_is_power_of_two(capacity) ? (capacity - 1) : 0;

    memset(entries, 0, capacity * sizeof(*entries));
    return 0;
}

static size_t ht_str_index(const ht_str_table_t* table, const uint32_t hash, const size_t i)
{
    if (table->mask != 0)
        return (hash + i) & table->mask;
    return (hash + i) % table->capacity;
}

int ht_str_insert(ht_str_table_t* table, const char* key, const char* value)
{
    if (!table || !table->entries || table->capacity == 0 || !key)
        return -1;

    const uint32_t hash = ht_str_hash(key);
    for (size_t i = 0; i < table->capacity; ++i)
    {
        const size_t idx = ht_str_index(table, hash, i);
        ht_str_entry_t* entry = &table->entries[idx];
        if (entry->key == nullptr)
        {
            entry->key = key;
            entry->value = value;
            table->count++;
            return 0;
        }
        if (strcmp(entry->key, key) == 0)
        {
            entry->value = value;
            return 0;
        }
    }
    return -1;
}

const char* ht_str_get(const ht_str_table_t* table, const char* key)
{
    if (!table || !table->entries || table->capacity == 0 || !key)
        return nullptr;

    const uint32_t hash = ht_str_hash(key);
    for (size_t i = 0; i < table->capacity; ++i)
    {
        const size_t idx = ht_str_index(table, hash, i);
        const ht_str_entry_t* entry = &table->entries[idx];
        if (entry->key == nullptr)
            return nullptr;
        if (strcmp(entry->key, key) == 0)
            return entry->value;
    }
    return nullptr;
}
