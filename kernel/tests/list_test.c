#include <tests/test.h>
#include <lib/list.h>

typedef struct
{
    int value;
    list_item_t link;
} test_node_t;

TEST(test_list_init_empty)
{
    LIST_HEAD(head);
    TEST_ASSERT(list_empty(&head));
    return true;
}

TEST(test_list_add_and_iterate)
{
    LIST_HEAD(head);
    test_node_t a = {.value = 1};
    test_node_t b = {.value = 2};
    test_node_t c = {.value = 3};

    list_add(&a.link, &head);
    list_add(&b.link, &head);
    list_add(&c.link, &head);

    TEST_ASSERT(!list_empty(&head));

    // list_add inserts at head, so order is c, b, a
    int i = 0;
    test_node_t *pos;
    list_foreach_entry(pos, &head, link)
    {
        int expected[] = {3, 2, 1};
        TEST_ASSERT(pos->value == expected[i++]);
    }
    TEST_ASSERT(i == 3);
    return true;
}

TEST(test_list_add_tail_order)
{
    LIST_HEAD(head);
    test_node_t a = {.value = 1};
    test_node_t b = {.value = 2};
    test_node_t c = {.value = 3};

    list_add_tail(&a.link, &head);
    list_add_tail(&b.link, &head);
    list_add_tail(&c.link, &head);

    // list_add_tail appends, so order is a, b, c
    int i = 0;
    test_node_t *pos;
    list_foreach_entry(pos, &head, link)
    {
        int expected[] = {1, 2, 3};
        TEST_ASSERT(pos->value == expected[i++]);
    }
    TEST_ASSERT(i == 3);
    return true;
}

TEST(test_list_del_middle)
{
    LIST_HEAD(head);
    test_node_t a = {.value = 1};
    test_node_t b = {.value = 2};
    test_node_t c = {.value = 3};

    list_add_tail(&a.link, &head);
    list_add_tail(&b.link, &head);
    list_add_tail(&c.link, &head);

    list_del(&b.link);

    int i = 0;
    test_node_t *pos;
    list_foreach_entry(pos, &head, link)
    {
        int expected[] = {1, 3};
        TEST_ASSERT(pos->value == expected[i++]);
    }
    TEST_ASSERT(i == 2);
    return true;
}

TEST(test_list_del_all_becomes_empty)
{
    LIST_HEAD(head);
    test_node_t a = {.value = 1};
    test_node_t b = {.value = 2};

    list_add(&a.link, &head);
    list_add(&b.link, &head);

    list_del(&a.link);
    list_del(&b.link);

    TEST_ASSERT(list_empty(&head));
    return true;
}

TEST(test_list_foreach_entry_safe_removal)
{
    LIST_HEAD(head);
    test_node_t nodes[4];
    for (int i = 0; i < 4; i++)
    {
        nodes[i].value = i;
        list_add_tail(&nodes[i].link, &head);
    }

    // Remove even-valued nodes during iteration
    test_node_t *pos, *tmp;
    list_foreach_entry_safe(pos, tmp, &head, link)
    {
        if (pos->value % 2 == 0)
            list_del(&pos->link);
    }

    // Only odd values remain: 1, 3
    int i = 0;
    list_foreach_entry(pos, &head, link)
    {
        int expected[] = {1, 3};
        TEST_ASSERT(pos->value == expected[i++]);
    }
    TEST_ASSERT(i == 2);
    return true;
}

TEST(test_list_foreach_entry_reverse)
{
    LIST_HEAD(head);
    test_node_t a = {.value = 1};
    test_node_t b = {.value = 2};
    test_node_t c = {.value = 3};

    list_add_tail(&a.link, &head);
    list_add_tail(&b.link, &head);
    list_add_tail(&c.link, &head);

    int i = 0;
    test_node_t *pos;
    list_foreach_entry_reverse(pos, &head, link)
    {
        int expected[] = {3, 2, 1};
        TEST_ASSERT(pos->value == expected[i++]);
    }
    TEST_ASSERT(i == 3);
    return true;
}

TEST(test_list_container_of)
{
    test_node_t node = {.value = 42};
    list_item_t *link_ptr = &node.link;
    test_node_t *recovered = container_of(link_ptr, test_node_t, link);
    TEST_ASSERT(recovered == &node);
    TEST_ASSERT(recovered->value == 42);
    return true;
}
