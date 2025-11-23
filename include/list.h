#pragma once

#include <stdbool.h>
#include <stddef.h>

typedef struct list_head
{
    struct list_head *next, *prev;
} list_head_t;

#define LIST_HEAD_INIT(name) {&(name), &(name)}

#define LIST_HEAD(name) \
    list_head_t name = LIST_HEAD_INIT(name)

static inline void INIT_LIST_HEAD(list_head_t *list)
{
    list->next = list;
    list->prev = list;
}

static inline void __list_add(list_head_t *new,
                              list_head_t *prev,
                              list_head_t *next)
{
    next->prev = new;
    new->next = next;
    new->prev = prev;
    prev->next = new;
}

static inline void list_add(list_head_t *new, list_head_t *head)
{
    __list_add(new, head, head->next);
}

static inline void list_add_tail(list_head_t *new, list_head_t *head)
{
    __list_add(new, head->prev, head);
}

static inline void __list_del(list_head_t *prev, list_head_t *next)
{
    next->prev = prev;
    prev->next = next;
}

static inline void list_del(list_head_t *entry)
{
    __list_del(entry->prev, entry->next);
    entry->next = NULL;
    entry->prev = NULL;
}

static inline bool list_empty(const list_head_t *head)
{
    return head->next == head;
}

#define container_of(ptr, type, member) ({                      \
        const typeof( ((type *)0)->member ) *__mptr = (ptr);    \
        (type *)( (char *)__mptr - offsetof(type,member) ); })

#define list_entry(ptr, type, member) \
    container_of(ptr, type, member)

#define list_first_entry(ptr, type, member) \
    list_entry((ptr)->next, type, member)

#define list_for_each(pos, head) \
    for (pos = (head)->next; pos != (head); pos = pos->next)

#define list_for_each_entry(pos, head, member)                 \
    for (pos = list_entry((head)->next, typeof(*pos), member); \
         &pos->member != (head);                               \
         pos = list_entry(pos->member.next, typeof(*pos), member))

#define list_for_each_entry_safe(pos, n, head, member)          \
    for (pos = list_entry((head)->next, typeof(*pos), member),  \
        n = list_entry(pos->member.next, typeof(*pos), member); \
         &pos->member != (head);                                \
         pos = n, n = list_entry(n->member.next, typeof(*n), member))

#define list_for_each_entry_reverse(pos, head, member)         \
    for (pos = list_entry((head)->prev, typeof(*pos), member); \
         &pos->member != (head);                               \
         pos = list_entry(pos->member.prev, typeof(*pos), member))
