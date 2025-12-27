#include <list.h>
#include <stdlib.h>

list_t *list_new(void)
{
    list_t *list = (list_t *)malloc(sizeof(list_t));
    if (!list) {
        return list;
    }

    list->count = 0;
    list->root_node = nullptr;

    return list;
}

int list_add(list_t *list, void *payload)
{
    list_node_t *new_node = list_node_new(payload);
    if (!new_node) {
        return 0;
    }

    if (!list->root_node) {
        list->root_node = new_node;
    } else {
        list_node_t *current_node = list->root_node;

        while (current_node->next)
            current_node = current_node->next;

        current_node->next = new_node;
        new_node->prev = current_node;
    }

    list->count++;

    return 1;
}

void *list_get_at(list_t *list, unsigned int index)
{
    if (list->count == 0 || index >= list->count) {
        return nullptr;
    }

    list_node_t *current_node = list->root_node;

    for (unsigned int current_index = 0; (current_index < index) && current_node; current_index++)
        current_node = current_node->next;

    return current_node ? current_node->payload : nullptr;
}

int list_find(list_t *list, void *payload)
{
    if (list->count == 0) {
        return -1;
    }

    list_node_t *current_node = list->root_node;

    for (unsigned int current_index = 0; current_index < list->count && current_node; current_index++) {

        if (current_node->payload == payload) {
            return (int)current_index;
        }

        current_node = current_node->next;
    }

    return -1;
}

void list_free(list_t *list)
{
    while (list->count) {
        free(list_remove_at(list, 0));
    }
    free(list);
}

void *list_remove_at(list_t *list, unsigned int index)
{
    if (list->count == 0 || index >= list->count) {
        return nullptr;
    }

    list_node_t *current_node = list->root_node;

    for (unsigned int current_index = 0; (current_index < index) && current_node; current_index++)
    {
        current_node = current_node->next;
    }

    if (!current_node) {
        return nullptr;
    }

    // NOLINTNEXTLINE(clang-analyzer-unix.Malloc): False positive - current_node is valid here
    if (current_node->prev) {
        current_node->prev->next = current_node->next;
    } else {
        list->root_node = current_node->next;
    }

    if (current_node->next) {
        current_node->next->prev = current_node->prev;
    }

    void *payload = current_node->payload;
    free(current_node);

    list->count--;

    return payload;
}

list_node_t *list_node_new(void *payload)
{
    list_node_t *node = (list_node_t *)malloc(sizeof(list_node_t));
    if (!node) {
        return nullptr;
    }

    node->payload = payload;
    node->prev = nullptr;
    node->next = nullptr;

    return node;
}

