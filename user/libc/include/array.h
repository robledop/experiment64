#pragma once
#include <assert.h>
#include <stdlib.h>
#include <string.h>

typedef struct
{
    unsigned long long magic;
    size_t count;
    size_t capacity;
} array_header_t;

/** @brief Initial capacity used when creating a new dynamic array. */
#define ARR_INIT_CAPACITY 1
/** @brief Multiplicative growth factor used during reallocation. */
#define ARR_GROW_FACTOR 2
/** @brief Maximum representable size_t value. */
#define ARR_SIZE_MAX ((size_t)-1)
/** @brief Debug magic stored in each array header. */
#define ARR_HEADER_MAGIC 0x4702534158488423ull

[[gnu::used]]
static inline void arr__assert_header_valid(const array_header_t *header)
{
    (void)header;
    assert(header != nullptr, "array header is null");
    assert(header->magic == ARR_HEADER_MAGIC, "array header magic mismatch");
    assert(header->count <= header->capacity, "array header count exceeds capacity");
}

[[gnu::used]]
static inline array_header_t *arr__header(void *arr)
{
    if (!arr)
        return nullptr;

    array_header_t *header = (array_header_t *)arr - 1;
    arr__assert_header_valid(header);
    return header;
}

[[gnu::used]]
static inline const array_header_t *arr__header_const(const void *arr)
{
    if (!arr)
        return nullptr;

    const array_header_t *header = (const array_header_t *)arr - 1;
    arr__assert_header_valid(header);
    return header;
}

[[gnu::used]]
static inline size_t arr__len(const void *arr)
{
    const array_header_t *header = arr__header_const(arr);
    return header ? header->count : 0;
}

[[gnu::used]]
static inline bool arr__empty(const void *arr)
{
    return arr__len(arr) == 0;
}

[[gnu::used]]
static inline void arr__clear(void *arr)
{
    array_header_t *header = arr__header(arr);
    if (!header)
        return;

    header->count = 0;
}

[[gnu::used]]
static inline bool arr__ensure_capacity(void **arr, size_t elem_size, size_t min_capacity)
{
    if (!arr || elem_size == 0)
        return false;

    if (min_capacity == 0)
        return true;

    array_header_t *header = arr__header(*arr);
    if (!header) {
        size_t capacity = ARR_INIT_CAPACITY;
        while (capacity < min_capacity) {
            if (capacity > ARR_SIZE_MAX / ARR_GROW_FACTOR)
                return false;
            capacity *= ARR_GROW_FACTOR;
        }

        if (capacity > (ARR_SIZE_MAX - sizeof(array_header_t)) / elem_size)
            return false;

        size_t alloc_size = sizeof(array_header_t) + (elem_size * capacity);
        header            = malloc(alloc_size);
        if (!header)
            return false;

        header->magic    = ARR_HEADER_MAGIC;
        header->count    = 0;
        header->capacity = capacity;
        *arr             = (void *)(header + 1);
        return true;
    }

    size_t capacity = header->capacity;
    if (capacity == 0)
        capacity = ARR_INIT_CAPACITY;

    if (capacity >= min_capacity) {
        header->capacity = capacity;
        return true;
    }

    while (capacity < min_capacity) {
        if (capacity > ARR_SIZE_MAX / ARR_GROW_FACTOR)
            return false;
        capacity *= ARR_GROW_FACTOR;
    }

    if (capacity > (ARR_SIZE_MAX - sizeof(array_header_t)) / elem_size)
        return false;

    size_t alloc_size = sizeof(array_header_t) + (elem_size * capacity);
    auto realloc_h    = realloc(header, alloc_size);
    if (!realloc_h)
        return false;

    header           = realloc_h;
    header->magic    = ARR_HEADER_MAGIC;
    header->capacity = capacity;
    *arr             = (void *)(header + 1);
    return true;
}

[[gnu::used]]
static inline void *arr__append_slot(void **arr, size_t elem_size)
{
    if (!arr || elem_size == 0)
        return nullptr;

    size_t next_count = arr__len(*arr) + 1;
    if (!arr__ensure_capacity(arr, elem_size, next_count))
        return nullptr;

    array_header_t *header = arr__header(*arr);
    void *slot             = (char *)(*arr) + (header->count * elem_size);
    header->count++;
    return slot;
}

[[gnu::used]]
static inline void *arr__insert_at_slot(void **arr, size_t elem_size, size_t idx)
{
    if (!arr || elem_size == 0)
        return nullptr;

    size_t count = arr__len(*arr);
    if (idx > count)
        return nullptr;

    if (!arr__ensure_capacity(arr, elem_size, count + 1))
        return nullptr;

    array_header_t *header = arr__header(*arr);
    char *base             = (char *)(*arr);
    if (idx < header->count) {
        memmove(base + ((idx + 1) * elem_size),
                base + (idx * elem_size),
                (header->count - idx) * elem_size);
    }

    header->count++;
    return base + (idx * elem_size);
}

static inline bool arr__remove_at(void *arr, size_t elem_size, size_t idx)
{
    if (!arr || elem_size == 0)
        return false;

    array_header_t *header = arr__header(arr);
    if (!header || idx >= header->count)
        return false;

    size_t tail_count = header->count - idx - 1;
    if (tail_count > 0) {
        memmove((char *)arr + (idx * elem_size),
                (char *)arr + ((idx + 1) * elem_size),
                tail_count * elem_size);
    }

    header->count--;
    return true;
}

[[gnu::used]]
static inline bool arr__pop(void *arr)
{
    array_header_t *header = arr__header(arr);
    if (!header || header->count == 0)
        return false;

    header->count--;
    return true;
}

[[gnu::used]]
static inline const void *arr__front_ptr_const(const void *arr, size_t elem_size)
{
    (void)elem_size;
    const array_header_t *header = arr__header_const(arr);
    if (!header)
        return nullptr;

    assert(header->count > 0, "arr_front on empty array");
    if (header->count == 0)
        return nullptr;

    return arr;
}

[[gnu::used]]
static inline const void *arr__back_ptr_const(const void *arr, size_t elem_size)
{
    const array_header_t *header = arr__header_const(arr);
    if (!header || elem_size == 0)
        return nullptr;

    assert(header->count > 0, "arr_back on empty array");
    if (header->count == 0)
        return nullptr;

    return (const char *)arr + ((header->count - 1) * elem_size);
}

[[gnu::used]]
static inline const void *arr__get_ptr_const(const void *arr, size_t elem_size, size_t idx)
{
    const array_header_t *header = arr__header_const(arr);
    if (!header || elem_size == 0)
        return nullptr;

    assert(idx < header->count, "arr_get index out of bounds");
    if (idx >= header->count)
        return nullptr;

    return (const char *)arr + (idx * elem_size);
}

[[gnu::used]]
static inline void *arr__get_ptr(void *arr, size_t elem_size, size_t idx)
{
    array_header_t *header = arr__header(arr);
    if (!header || elem_size == 0)
        return nullptr;

    assert(idx < header->count, "arr_set index out of bounds");
    if (idx >= header->count)
        return nullptr;

    return (char *)arr + (idx * elem_size);
}

[[gnu::used]]
static inline void arr__free(void **arr)
{
    if (!arr || !*arr)
        return;

    array_header_t *header = arr__header(*arr);
    if (!header)
        return;

    header->magic = 0;
    free(header);
    *arr = nullptr;
}

/** @brief Append one value to the end of the array. */
#define arr_push(arr, x) do {                                                                                  \
        auto __arr_slot = (typeof((arr)[0]) *)arr__append_slot((void**)&(arr), sizeof(*(arr)));              \
        if (__arr_slot)                                                                                        \
            *__arr_slot = (x);                                                                                 \
    } while(0)

/** @brief Remove the element at index @p idx, preserving order. */
#define arr_remove_at(arr, idx) do {                                                                           \
        (void)arr__remove_at((arr), sizeof(*(arr)), (size_t)(idx));                                           \
    } while(0)

/** @brief Remove all elements equal to @p val, preserving order. */
#define arr_remove(arr, val) do {                                                                              \
        auto __arr_remove_value = (val);                                                                       \
        size_t __arr_remove_idx = 0;                                                                           \
        while (__arr_remove_idx < arr_len(arr)) {                                                              \
            if (arr_get((arr), __arr_remove_idx) == __arr_remove_value) {                                     \
                arr_remove_at((arr), __arr_remove_idx);                                                        \
            } else {                                                                                            \
                __arr_remove_idx++;                                                                            \
            }                                                                                                  \
        }                                                                                                      \
    } while(0)

/** @brief Insert @p val at index @p idx, shifting following elements right. */
#define arr_insert_at(arr, idx, val) do {                                                                      \
        auto __arr_slot =                                                                                       \
            (typeof((arr)[0]) *)arr__insert_at_slot((void**)&(arr), sizeof(*(arr)), (size_t)(idx));          \
        if (__arr_slot)                                                                                        \
            *__arr_slot = (val);                                                                               \
    } while(0)

/** @brief Remove the last element if the array is non-empty. */
#define arr_pop(arr) do {                                                                                      \
        (void)arr__pop((arr));                                                                                 \
    } while(0)

/** @brief Return the current element count. */
#define arr_len(arr) arr__len((arr))
/** @brief Return true when the array has no elements. */
#define arr_empty(arr) arr__empty((arr))
/** @brief Remove all elements and keep the current capacity. */
#define arr_clear(arr) do {                                                                                    \
        arr__clear((arr));                                                                                     \
    } while(0)
/** @brief Return the first element. Asserts in debug if empty. */
#define arr_front(arr) (*((typeof((arr)[0]) *)arr__front_ptr_const((arr), sizeof(*(arr)))))
/** @brief Return the last element. Asserts in debug if empty. */
#define arr_back(arr) (*((typeof((arr)[0]) *)arr__back_ptr_const((arr), sizeof(*(arr)))))
/** @brief Free array storage and set the array pointer to nullptr. */
#define arr_free(arr) do {                                                                                     \
        arr__free((void**)&(arr));                                                                             \
    } while(0)
/** @brief Return the element at index @p idx. Asserts in debug if out of bounds. */
#define arr_get(arr, idx) (*((typeof((arr)[0]) *)arr__get_ptr_const((arr), sizeof(*(arr)), (size_t)(idx))))
/** @brief Assign a value to index @p idx using `arr[idx] = value` semantics. */
#define arr_set(arr, idx, val) do {                                                                            \
        auto __arr_set_ptr = (typeof((arr)[0]) *)arr__get_ptr((arr), sizeof(*(arr)), (size_t)(idx));         \
        if (__arr_set_ptr)                                                                                     \
            *__arr_set_ptr = (val);                                                                            \
    } while(0)
