#pragma once

typedef struct list_node
{
    void *payload;
    struct list_node *prev;
    struct list_node *next;
} list_node_t;

typedef struct list
{
    unsigned int count;
    list_node_t *root_node;
} list_t;

list_t *list_new(void);
int list_add(list_t *list, void *payload);
void *list_get_at(list_t *list, unsigned int index);
void *list_remove_at(list_t *list, unsigned int index);
int list_find(list_t *list, void *payload);
void list_free(list_t *list);
list_node_t *list_node_new(void *payload);

