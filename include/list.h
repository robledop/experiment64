#pragma once


#include <stddef.h>

#define UNUSED __attribute__((unused)) // To silence unused function warnings

typedef struct list_item
{
    struct list_item *next, *prev;
} list_item_t;

/**
 * Compile-time initializer for a list head (use for static/global variables).
 * For runtime initialization, use list_init_head() instead.
 */
#define LIST_HEAD_INIT(name) {&(name), &(name)}

#define LIST_HEAD(name) \
    list_item_t name = LIST_HEAD_INIT(name)

/**
 * Initialize a list head.
 * @param list The list head to initialize.
 */
static inline UNUSED void list_init_head(list_item_t* list)
{
    list->next = list;
    list->prev = list;
}

// ReSharper disable once CppDFAUnreachableFunctionCall
static inline void list_add_helper(list_item_t* new,
                              list_item_t* prev,
                              list_item_t* next)
{
    next->prev = new;
    new->next = next;
    new->prev = prev;
    prev->next = new;
}

/**
 * Add a new entry at the head of a list.
 * @param new The new entry to add.
 * @param head The head of the list.
 */
static inline UNUSED void list_add(list_item_t* new, list_item_t* head)
{
    list_add_helper(new, head, head->next);
}

/**
 * Add a new entry at the tail of a list.
 * @param new The new entry to add.
 * @param head The head of the list.
 */
static inline UNUSED void list_add_tail(list_item_t* new, list_item_t* head)
{
    list_add_helper(new, head->prev, head);
}

/**
 * Remove an entry from a list.
 * @param entry The entry to remove from the list.
 */
static inline UNUSED void list_del(list_item_t* entry)
{
    entry->next->prev = entry->prev;
    entry->prev->next = entry->next;
    entry->next = nullptr;
    entry->prev = nullptr;
}

/**
 * Check if a list is empty.
 * @param head The head of the list.
 * @return True if the list is empty, false otherwise.
 */
static inline UNUSED bool list_empty(const list_item_t* head)
{
    return head->next == head;
}

/**
 * Get the container structure from a member pointer.
 * @param ptr - Pointer to the member within the container structure
 * @param type - Type of the container structure
 * @param member - Name of the member within the container structure
 */
#define container_of(ptr, type, member) __extension__({                     \
        const typeof(((type *)0)->member) *__mptr = (ptr);                  \
        (type *)((char *)__mptr - offsetof(type, member)); })

/**
 * Get the container structure from a member pointer.
 * @param ptr - Pointer to the member within the container structure
 * @param type - Type of the container structure
 * @param member - Name of the member within the container structure
 */
#define list_entry(ptr, type, member) \
    container_of(ptr, type, member)

#define list_first_entry(ptr, type, member) \
    list_entry((ptr)->next, type, member)

#define list_for_each(pos, head) \
    for ((pos) = (head)->next; (pos) != (head); (pos) = (pos)->next)

// UBSAN-friendly iteration that avoids container_of on the list head when empty and
// does not introduce helper variables that shadow in nested loops.
#define list_for_each_entry(pos, head, member)                                                   \
    for ((pos) = list_empty(head) ? NULL : list_entry((head)->next, typeof(*(pos)), member);     \
         (pos) != NULL;                                                                          \
         (pos) = ((pos)->member.next == (head)) ? nullptr : list_entry((pos)->member.next, typeof(*(pos)), member))

#define list_for_each_entry_safe(pos, n, head, member)                                           \
    for ((pos) = list_empty(head) ? nullptr : list_entry((head)->next, typeof(*(pos)), member),     \
         (n) = (pos) ? (((pos)->member.next == (head)) ? nullptr : list_entry((pos)->member.next, typeof(*(n)), member)) : nullptr; \
         (pos) != nullptr;                                                                          \
         (pos) = (n),                                                                            \
         (n) = (pos) ? (((pos)->member.next == (head)) ? nullptr : list_entry((pos)->member.next, typeof(*(n)), member)) : nullptr)

#define list_for_each_entry_reverse(pos, head, member)                                           \
    for ((pos) = list_empty(head) ? nullptr : list_entry((head)->prev, typeof(*(pos)), member);     \
         (pos) != nullptr;                                                                          \
         (pos) = ((pos)->member.prev == (head)) ? nullptr : list_entry((pos)->member.prev, typeof(*(pos)), member))
