#include <tests/test.h>
#include <lib/sort.h>

static int cmp_int(const void *a, const void *b)
{
    int ia = *(const int *)a;
    int ib = *(const int *)b;
    return (ia > ib) - (ia < ib);
}

TEST(test_qsort_basic)
{
    int arr[] = {5, 3, 1, 4, 2};
    qsort(arr, 5, sizeof(int), cmp_int);
    for (int i = 0; i < 4; i++)
        TEST_ASSERT(arr[i] <= arr[i + 1]);
    TEST_ASSERT(arr[0] == 1 && arr[4] == 5);
    return true;
}

TEST(test_qsort_already_sorted)
{
    int arr[] = {1, 2, 3, 4, 5};
    qsort(arr, 5, sizeof(int), cmp_int);
    for (int i = 0; i < 5; i++)
        TEST_ASSERT(arr[i] == i + 1);
    return true;
}

TEST(test_qsort_reverse)
{
    int arr[] = {5, 4, 3, 2, 1};
    qsort(arr, 5, sizeof(int), cmp_int);
    for (int i = 0; i < 5; i++)
        TEST_ASSERT(arr[i] == i + 1);
    return true;
}

TEST(test_qsort_single_element)
{
    int arr[] = {42};
    qsort(arr, 1, sizeof(int), cmp_int);
    TEST_ASSERT(arr[0] == 42);
    return true;
}

TEST(test_qsort_duplicates)
{
    int arr[] = {3, 1, 3, 2, 1};
    qsort(arr, 5, sizeof(int), cmp_int);
    TEST_ASSERT(arr[0] == 1 && arr[1] == 1);
    TEST_ASSERT(arr[2] == 2);
    TEST_ASSERT(arr[3] == 3 && arr[4] == 3);
    return true;
}

TEST(test_qsort_empty)
{
    int arr[] = {1};
    qsort(arr, 0, sizeof(int), cmp_int);
    TEST_ASSERT(arr[0] == 1); // unchanged
    return true;
}
