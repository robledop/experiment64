#include "sort.h"
#include "string.h"

static void swap(void *a, void *b, size_t size)
{
    char temp[256]; // Fixed buffer for swapping, should be enough for most cases
    // If size > 256, we need a loop or dynamic allocation (but we don't want to depend on malloc here if possible)

    if (size <= sizeof(temp))
    {
        memcpy(temp, a, size);
        memcpy(a, b, size);
        memcpy(b, temp, size);
    }
    else
    {
        // Fallback for large elements
        char *p = a;
        char *q = b;
        for (size_t i = 0; i < size; i++)
        {
            char t = p[i];
            p[i] = q[i];
            q[i] = t;
        }
    }
}

static void quicksort(void *base, size_t size, int (*compar)(const void *, const void *), int left, int right)
{
    if (left >= right)
        return;

    int i = left;
    int j = right;
    char *pivot = (char *)base + ((left + right) / 2) * size;

    // We need to copy pivot because swap might move it
    // But wait, if we swap the element at pivot index, 'pivot' pointer points to new value.
    // Standard way is to pick pivot value.
    // Since we don't want to malloc, let's use the median-of-three or just middle element.
    // To be safe against moving pivot, let's copy it to stack if small, or be careful.

    // Let's use a simpler partition scheme (Lomuto or Hoare).
    // Hoare partition:

    // We need a temporary buffer for pivot if we want to be safe, or just use index.
    // But comparing requires passing pointers.

    // Let's use a simple implementation that is easy to get right.
    // Insertion sort for small arrays is also good.

    // Let's implement a simple partition.

    char *ptr = (char *)base;

    // Pivot selection: middle element
    int pivot_idx = (left + right) / 2;

    // Move pivot to end
    swap(ptr + pivot_idx * size, ptr + right * size, size);

    int store_idx = left;
    for (int k = left; k < right; k++)
    {
        if (compar(ptr + k * size, ptr + right * size) < 0)
        {
            swap(ptr + k * size, ptr + store_idx * size, size);
            store_idx++;
        }
    }

    swap(ptr + store_idx * size, ptr + right * size, size);

    quicksort(base, size, compar, left, store_idx - 1);
    quicksort(base, size, compar, store_idx + 1, right);
}

void qsort(void *base, size_t nmemb, size_t size, int (*compar)(const void *, const void *))
{
    if (nmemb < 2 || size == 0)
        return;

    quicksort(base, size, compar, 0, nmemb - 1);
}
