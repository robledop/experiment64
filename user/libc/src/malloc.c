#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <limits.h>
#include <stdint.h>

typedef struct block_meta
{
    size_t size;
    struct block_meta *next;
    int free;
} block_meta;

#define META_SIZE sizeof(block_meta)

static block_meta *global_base = nullptr;

static block_meta *find_free_block(block_meta **last, size_t size)
{
    block_meta *current = global_base;
    while (current && !(current->free && current->size >= size))
    {
        *last = current;
        current = current->next;
    }
    return current;
}

static block_meta *request_space(block_meta *last, size_t size)
{
    block_meta* block = sbrk(0);
    if (size > (size_t)INTPTR_MAX - META_SIZE)
        return nullptr;
    intptr_t increment = (intptr_t)(size + META_SIZE);
    const void *request = sbrk(increment);
    if (request == (void *)-1)
    {
        return nullptr; // sbrk failed
    }

    if (request != block)
    {
        // This should not happen if sbrk is thread-safe or we are single threaded
    }

    block->size = size;
    block->next = nullptr;
    block->free = 0;

    if (last)
    {
        last->next = block;
    }
    return block;
}

void *malloc(size_t size)
{
    if (size <= 0)
    {
        return nullptr;
    }

    block_meta *block;

    if (!global_base)
    {
        block = request_space(nullptr, size);
        if (!block)
        {
            return nullptr;
        }
        global_base = block;
    }
    else
    {
        block_meta *last = global_base;
        block = find_free_block(&last, size);
        if (!block)
        {
            block = request_space(last, size);
            if (!block)
            {
                return nullptr;
            }
        }
        else
        {
            block->free = 0;
        }
    }

    return (block + 1);
}

void free(void *ptr)
{
    if (!ptr)
    {
        return;
    }

    block_meta *block_ptr = (block_meta *)ptr - 1;
    block_ptr->free = 1;

    // Simple coalescing with next block
    if (block_ptr->next && block_ptr->next->free)
    {
        block_ptr->size += META_SIZE + block_ptr->next->size;
        block_ptr->next = block_ptr->next->next;
    }
}

void *calloc(size_t nmemb, size_t size)
{
    size_t total_size = nmemb * size;
    void *ptr = malloc(total_size);
    if (ptr)
    {
        memset(ptr, 0, total_size);
    }
    return ptr;
}

void *realloc(void *ptr, size_t size)
{
    if (!ptr)
    {
        return malloc(size);
    }

    block_meta *block_ptr = (block_meta *)ptr - 1;
    if (block_ptr->size >= size)
    {
        return ptr;
    }

    void *new_ptr = malloc(size);
    if (!new_ptr)
    {
        return nullptr;
    }

    memcpy(new_ptr, ptr, block_ptr->size);
    free(ptr);
    return new_ptr;
}
